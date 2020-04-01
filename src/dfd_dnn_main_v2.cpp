#define _CRT_SECURE_NO_WARNINGS

#if defined(_WIN32) | defined(__WIN32__) | defined(__WIN32) | defined(_WIN64) | defined(__WIN64)
#include <windows.h>
//#pragma warning( disable : 4503 )
#endif

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <thread>
#include <sstream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <string>
#include <utility>
#include <atomic>

// Custom includes
#include "get_platform.h"
#include "file_ops.h"
#include "file_parser.h"
#include "get_current_time.h"
#include "num2string.h"

//#include "gorgon_capture.h"
#include "dlib_jet_functions.h"
#include "dfd_cropper.h"
#include "center_cropper.h"
#include "get_cuda_devices.h"
#include "apply_random_noise.h"
#include "array_image_operations.h"

// Net Version
// Things must go in this order since the array size is determined by the network header file
#include "dfd_net_v14.h"
//#include "dfd_net_lin_v01.h"
//#include "dfd_net_l2_v01.h"
#include "dfd_dnn.h"
#include "load_dfd_data.h"
#include "eval_dfd_net_performance.h"  

// dlib includes
#include <dlib/dnn.h>
#include <dlib/image_io.h>
#include <dlib/data_io.h>
#include <dlib/image_transforms.h>

// this is for enabling GUI support, i.e. opening windows
#ifndef DLIB_NO_GUI_SUPPORT
    #include <dlib/gui_widgets.h>
#endif

// -------------------------------GLOBALS--------------------------------------

extern const uint32_t img_depth;
extern const uint32_t secondary;
std::string platform;
std::vector<std::array<dlib::matrix<uint16_t>, img_depth>> tr_crop, te_crop;
std::vector<dlib::matrix<uint16_t>> gt_crop, gt_te_crop;

std::string version;
std::string net_name = "dfd_net_";
std::string net_sync_name = "dfd_sync_";
std::string logfileName = "dfd_net_";
std::string gorgon_savefile = "gorgon_dfd_";
std::string cropper_stats_file = "crop_stats_";

// --------------------------External Functions--------------------------------


// ----------------------------------------------------------------------------
void get_platform_control(void)
{
    get_platform(platform);
    
    if (platform == "")
	{
		std::cout << "No Platform could be identified... defaulting to Windows." << std::endl;
		platform = "Win";
	}
    
    version = version + platform;
    net_sync_name = net_sync_name + version;
    logfileName = logfileName + version + "_";
    net_name = net_name + version + ".dat";
    gorgon_savefile = gorgon_savefile + version + "_";
    cropper_stats_file = cropper_stats_file + version + ".bin";
}

//-----------------------------------------------------------------------------

void print_usage(void)
{
    std::cout << "Enter the following as arguments into the program:" << std::endl;
    std::cout << "<image file path> <infocus image> <out of focus image>" << std::endl;
    std::cout << endl;
}

//-----------------------------------------------------------------------------
/*
void counter(void)
{
    std::chrono::time_point<std::chrono::steady_clock> last_time = std::chrono::steady_clock::now();
    std::chrono::time_point<std::chrono::steady_clock> now_time = std::chrono::steady_clock::now();

    while (run)
    {
        now_time = std::chrono::steady_clock::now();
        if(now_time - last_time > std::chrono::seconds(300))
        {
            last_time = now_time;
            std::cout << "nr = " << nr;
            std::cout << "; tp = " << (double)(nr2/1e9) << std::endl;
        }
    }
}
*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
    uint64_t idx=0, jdx=0;

    std::string sdate, stime;

    std::ofstream DataLogStream;
    std::string train_inputfile;
    std::string test_inputfile;
    std::string train_data_directory, test_data_directory;
    std::string data_home = "";
    
    std::vector<std::vector<std::string>> training_file;
    std::vector<std::vector<std::string>> test_file;
    std::vector<std::pair<std::string, std::string>> tr_image_files, te_image_files;

    typedef std::chrono::duration<double> d_sec;
    auto start_time = chrono::system_clock::now();
    auto stop_time = chrono::system_clock::now();
    double training_duration;  // number of hours to train 
    auto elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);

    std::vector<dlib::matrix<uint16_t>> gt_train, gt_test;
    dlib::matrix<uint16_t> map;

    std::vector<std::array<dlib::matrix<uint16_t>, img_depth>> tr;
    std::vector<std::array<dlib::matrix<uint16_t>, img_depth>> te;

    std::vector<std::string> stop_codes = { "Minimum Learning Rate Reached.", "Max Training Time Reached", "Max Training Steps Reached" };
    std::vector<double> stop_criteria;
    training_params tp;
    crop_info ci;
    //uint64_t num_crops;
    //std::vector<std::pair<uint64_t, uint64_t>> crop_sizes; // height, width
    std::vector<uint32_t> filter_num;
    uint64_t max_one_step_count;
    uint32_t expansion_factor;
    double std = 1.0;
    std::vector<int32_t> gpu = { 0 };
    std::array<float, img_depth> avg_color;

    // these are the parameters to load in an image to make sure that it is the correct size
    // for the network.  The first number makes sure that the image is a modulus of the number
    // and the second number is an offest from the modulus.  This is used based on the network
    // structure (downsampling and upsampling tensor sizes).
    std::pair<uint32_t, uint32_t> mod_params(16, 0);

    // this is a check to see what CUDA version is being used
    // if it is version 10.1 or greater this code might work.  it fails with 10.0, but it could work with 8.5
    //if(CUDA_VERSION >= 10.1)

    //else
    const bool run_tests = true;

    //////////////////////////////////////////////////////////////////////////////////

    if (argc == 1)
    {
        print_usage();
        std::cin.ignore();
        return 0;
    }

    std::string parseFilename = argv[1];

    // parse through the supplied csv file
    parse_dnn_data_file(parseFilename, version, stop_criteria, tp, train_inputfile, test_inputfile, ci, avg_color, filter_num);
    training_duration = stop_criteria[0];
    max_one_step_count = (uint64_t)stop_criteria[1];

    // check the input scaling factors.  if they are the same then the expansion factor for the cropper is all 8
    // otherwise the expansion factor is 4
    if(ci.scale.first == ci.scale.second)
        expansion_factor = 8;
    else
        expansion_factor = 4;

    // modify crop sizes based on scales
    // this is done to make crop size input easier
    ci.train_crop_sizes.first = ci.train_crop_sizes.first * ci.scale.first;
    ci.train_crop_sizes.second = ci.train_crop_sizes.second * ci.scale.second;

    // check the platform
    get_platform_control();
    uint8_t HPC = 0;
    
    if(platform.compare(0,3,"HPC") == 0)
    {
        std::cout << "HPC Platform Detected." << std::endl;
        HPC = 1;
    }
    
    const std::string os_file_sep = "/";
    std::string program_root;
    std::string sync_save_location;
    std::string output_save_location;
    std::string gorgon_save_location;
    
    // setup save variable locations
#if defined(_WIN32) | defined(__WIN32__) | defined(__WIN32) | defined(_WIN64) | defined(__WIN64)
    program_root = get_path(get_path(get_path(std::string(argv[0]), "\\"), "\\"), "\\") + os_file_sep;
    sync_save_location = program_root + "nets/";
    output_save_location = program_root + "results/";
    gorgon_save_location = program_root + "gorgon_save/";

#else    
    if(HPC == 1)
    {
        //HPC version
        program_root = get_path(get_path(get_path(std::string(argv[0]), os_file_sep), os_file_sep), os_file_sep) + os_file_sep;
    }
    else
    {
        // Ubuntu
        program_root = "/home/owner/Projects/dfd_dnn/";
    }
    
    sync_save_location = program_root + "nets/";
    output_save_location = program_root + "results/";
    gorgon_save_location = program_root + "gorgon_save/";


#endif
    
    std::cout << "Reading Inputs... " << std::endl;
    std::cout << "Platform:             " << platform << std::endl;
    std::cout << "program_root:         " << program_root << std::endl;
    std::cout << "sync_save_location:   " << sync_save_location << std::endl;
    std::cout << "output_save_location: " << output_save_location << std::endl;
    std::cout << "gorgon_save_location: " << gorgon_save_location << std::endl << std::endl;


    try {
        
        get_current_time(sdate, stime);
        logfileName = logfileName + sdate + "_" + stime + ".txt";

        std::cout << "Log File:             " << (output_save_location + logfileName) << std::endl << std::endl;
        DataLogStream.open((output_save_location + logfileName), ios::out | ios::app);
        
        // Add the date and time to the start of the log file
        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream << "Version: 2.0    Date: " << sdate << "    Time: " << stime << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        std::cout << get_cuda_devices() << std::endl;

        DataLogStream << get_cuda_devices();
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        ///////////////////////////////////////////////////////////////////////////////
        // Step 1: Read in the training and testing images
        ///////////////////////////////////////////////////////////////////////////////
        // get the "DATA_HOME" environment variable <- location of the root data folder
        //data_home = path_check(get_env_variable("DATA_HOME"));
 
        std::cout << "train input file: " << train_inputfile << std::endl;
        // parse through the supplied training csv file
#if defined(_WIN32) | defined(__WIN32__) | defined(__WIN32) | defined(_WIN64) | defined(__WIN64)
        parse_csv_file(train_inputfile, training_file);
        // the first line in this file is now the data directory
        train_data_directory = data_home + training_file[0][0];
#else
        if (HPC == 1)
        {
            parse_csv_file(train_inputfile, training_file);
            train_data_directory = data_home + training_file[0][2];
        }
        else
        {
            parse_csv_file(train_inputfile, training_file);
            train_data_directory = data_home + training_file[0][1];
        }
#endif

        // remove the first line which was the data directory
        training_file.erase(training_file.begin());
        


        //std::cout << train_inputfile << std::endl;
        std::cout << "Training image sets to parse: " << training_file.size() << std::endl;
        
        DataLogStream << train_inputfile << std::endl;
        DataLogStream << "Training image sets to parse: " << training_file.size() << std::endl;

        std::cout << "Loading training images..." << std::endl;
        std::cout << "training data_directory:       " << train_data_directory << std::endl;
        
        start_time = chrono::system_clock::now();
        load_dfd_data(training_file, train_data_directory, mod_params, tr, gt_train, tr_image_files);
        stop_time = chrono::system_clock::now();

        elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);
        std::cout << "Loaded " << tr.size() << " training image sets in " << elapsed_time.count() / 60 << " minutes." << std::endl << std::endl;


//-----------------------------------------------------------------------------
        // load the test data
        std::cout << "test input file: " << test_inputfile << std::endl;
#if defined(_WIN32) | defined(__WIN32__) | defined(__WIN32) | defined(_WIN64) | defined(__WIN64)
        parse_csv_file(test_inputfile, test_file);
        test_data_directory = data_home + test_file[0][0];
#else
        if (HPC == 1)
        {
            parse_csv_file(test_inputfile, test_file);
            test_data_directory = data_home + test_file[0][2];
        }
        else
        {
            parse_csv_file(test_inputfile, test_file);
            test_data_directory = data_home + test_file[0][1];
        }
#endif

        // remove the first line which was the data directory
        test_file.erase(test_file.begin());

        //std::cout << test_inputfile << std::endl;
        std::cout << "Test image sets to parse: " << test_file.size() << std::endl;

        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream << test_inputfile << std::endl;
        DataLogStream << "Test image sets to parse: " << test_file.size() << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        std::cout << "Loading test images..." << std::endl;
        std::cout << "test data_directory:       " << test_data_directory << std::endl;

        start_time = chrono::system_clock::now();
        load_dfd_data(test_file, test_data_directory, mod_params, te, gt_test, te_image_files);
        stop_time = chrono::system_clock::now();

        elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);
        std::cout << "Loaded " << te.size() << " test image sets in " << elapsed_time.count() / 60 << " minutes." << std::endl << std::endl;
               
        ///////////////////////////////////////////////////////////////////////////////
        // Step 2: Setup the network
        ///////////////////////////////////////////////////////////////////////////////
        dlib::set_dnn_prefer_smallest_algorithms();

        // set the cuda device explicitly
        if (gpu.size() == 1)
            dlib::cuda::set_device(gpu[0]);

        // instantiate the network
        // load in the conv and cont filter numbers from the input file
        dfd_net_type dfd_net = config_net<dfd_net_type>(avg_color, filter_num);
        
        dlib::dnn_trainer<dfd_net_type, dlib::adam> trainer(dfd_net, dlib::adam(0.0005, 0.5, 0.99), gpu);
        //dlib::dnn_trainer<dfd_net_type, dlib::sgd> trainer(dfd_net, dlib::sgd(0.0005, 0.99));

        trainer.set_learning_rate(tp.intial_learning_rate);
        trainer.be_verbose();
        trainer.set_synchronization_file((sync_save_location + net_sync_name), std::chrono::minutes(5));
        trainer.set_iterations_without_progress_threshold(tp.steps_wo_progess);
        trainer.set_learning_rate_shrink_factor(tp.learning_rate_shrink_factor);
        trainer.set_test_iterations_without_progress_threshold(3000);
        trainer.set_max_num_epochs(65000);

        dfd_cropper cropper;
        cropper.set_chip_dims(ci.train_crop_sizes);
        cropper.set_seed(time(0));
        cropper.set_scale(ci.scale);
        cropper.set_expansion_factor(expansion_factor);
        //cropper.set_stats_filename((output_save_location + cropper_stats_file));

        std::cout << "Input Array Depth: " << img_depth << std::endl;
        std::cout << "Secondary data loading value: " << secondary << std::endl << std::endl;
        DataLogStream << "Input Array Depth: " << img_depth << std::endl;
        DataLogStream << "Secondary data loading value: " << secondary << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        std::cout << "Eval Crop Size: " << ci.eval_crop_sizes.first << "x" << ci.eval_crop_sizes.second << std::endl << std::endl;
        DataLogStream << "Eval Crop Size: " << ci.eval_crop_sizes.first << "x" << ci.eval_crop_sizes.second << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        std::cout << "Crop count: " << ci.crop_num << std::endl;
        DataLogStream << "Crop count: " << ci.crop_num << std::endl;

        std::cout << cropper << std::endl;
        DataLogStream << cropper << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;
        
        std::cout << trainer << std::endl;
        DataLogStream << trainer << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        std::cout << dfd_net << std::endl;

        DataLogStream << "Net Name: " << net_name << std::endl;
        DataLogStream << dfd_net << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;

        // write the training log header
        DataLogStream << "step, learning rate, average loss, Train (NMAE, NRMSE, SSIM), Test (NMAE, NRMSE, SSIM)" << std::endl;
        
        ///////////////////////////////////////////////////////////////////////////////
        // Step 3: Begin the training efforts
        ///////////////////////////////////////////////////////////////////////////////

        int32_t stop = -1;
        uint64_t count = 1;

        uint64_t test_step_count = 50;
        uint64_t gorgon_count = 500;

        //init_gorgon((sync_save_location + gorgon_savefile));

        std::cout << "Starting Training..." << std::endl;
        start_time = chrono::system_clock::now();

        dlib::rand rnd(time(NULL));

//-----------------------------------------------------------------------------
//          TRAINING START
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
        // these were used for trouble shooting       
        //run = true;
        //std::thread th1(counter);
//-----------------------------------------------------------------------------

        dlib::matrix<double, 1, 6> train_results = dlib::zeros_matrix<double>(1, 6);
        dlib::matrix<double, 1, 6> test_results = dlib::zeros_matrix<double>(1, 6);

        while(stop < 0)       
        {

            if (trainer.get_learning_rate() >= tp.final_learning_rate)
            {
                cropper(ci.crop_num, tr, gt_train, tr_crop, gt_crop);

                //@mem((gt_crop[0].data).data,UINT16,1,gt_crop[0].nc(), gt_crop[0].nr(),gt_crop[0].nc()*2)
                //@mem((tr_crop[0][0].data).data,UINT16,1,tr_crop[0][0].nc(), tr_crop[0][0].nr(),tr_crop[0][0].nc()*2)

                // apply a random noise to the image
                //for (auto&& tc : tr_crop)
                //{
                //    apply_random_noise((uint8_t)0, (uint8_t)255, tc, rnd, std);
                //}

                trainer.train_one_step(tr_crop, gt_crop);
            }
            else
            {
                stop = 0;
            }

            stop_time = chrono::system_clock::now();
            elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);
            
            if((double)elapsed_time.count()/(double)3600.0 > training_duration)
            {
                stop = 1;
            }

            uint64_t one_step_calls = trainer.get_train_one_step_calls();
            
            /*
            // this section once worked to test the performance of the network during training
            // but now it is broken somehow
            if(((one_step_calls % test_step_count) == 0) && (run_tests == true))
            {
                
                //std::cout << "------------------------------------------------------------------" << std::endl;
                //trainer.test_one_step(tr_crop, gt_crop);

                // run the training and test images through the network to evaluate the intermediate performance
                train_results = eval_all_net_performance(dfd_net, tr, gt_train, ci.eval_crop_sizes, ci.scale);
                test_results = eval_all_net_performance(dfd_net, te, gt_test, ci.eval_crop_sizes, ci.scale);

                //trainer.test_one_step(tr_crop, gt_crop);
                //dfd_net(tr_crop);

                // start logging the results
                DataLogStream << std::setw(6) << std::setfill('0') << one_step_calls << ", ";
                DataLogStream << std::fixed << std::setprecision(10) << trainer.get_learning_rate() << ", ";
                DataLogStream << std::fixed << std::setprecision(5) << trainer.get_average_loss() << ", ";

                std::cout << "Training Results (NMAE, NRMSE, SSIM, Var_GT, Var_DM): " << std::fixed << std::setprecision(5) << train_results(0, 0) << ", " << train_results(0, 1)
                    << ", " << train_results(0, 2) << ", " << train_results(0, 4) << ", " << train_results(0, 5) << std::endl;
                std::cout << "Testing Results (NMAE, NRMSE, SSIM, Var_GT, Var_DM):  " << std::fixed << std::setprecision(5) << test_results(0, 0) << ", " << test_results(0, 1)
                    << ", " << test_results(0, 2) << ", " << test_results(0, 4) << ", " << test_results(0, 5) << std::endl;
                std::cout << "------------------------------------------------------------------" << std::endl;

                DataLogStream << std::fixed << std::setprecision(5) << train_results(0, 0) << ", " << train_results(0, 1) << ", "
                    << train_results(0, 2) << ", " << train_results(0, 4) << ", " << train_results(0, 5) << ", ";
                DataLogStream << std::fixed << std::setprecision(5) << test_results(0, 0) << ", " << test_results(0, 1) << ", "
                    << test_results(0, 2) << ", " << test_results(0, 4) << ", " << test_results(0, 5) << std::endl;

                // run a single image through to save the progress
                //std::array<dlib::matrix<uint16_t>, img_depth> tec;
                //uint32_t crop_w = 450, crop_h = 360;
                //center_cropper(te[1], tec, crop_w, crop_h);
                //dlib::matrix<uint16_t> map = dfd_net(te[1]);

                std::string map_save_file = output_save_location + "test_save_" + version + "_" + num2str(one_step_calls, "%06d") + ".png";
                //dlib::save_png(dlib::matrix_cast<uint8_t>(map), map_save_file);


            }
            */

            // gorgon test
            if ((one_step_calls % gorgon_count) == 0)
            {
                //save_gorgon(dfd_net, one_step_calls);
            }

            if (one_step_calls >= max_one_step_count)
            {
                stop = 2;
            }

            //if (one_step_calls >= lr_sched[count].first)
            //{
            //    trainer.set_learning_rate(lr_sched[count].second);
            //    ++count;
            //}

        }   // end of while loop

//-----------------------------------------------------------------------------
        // these were used for trouble shooting
        //run = false;
        //th1.join();
//-----------------------------------------------------------------------------
        //close_gorgon();

        cropper.close_cropper_stream();

        te_crop.clear();
        gt_te_crop.clear();

		std::cout << std::endl << "------------------------------------------------------------------" << std::endl;
        std::cout << "Stop Code: " << stop_codes[stop] << std::endl;
        std::cout << "Final Average Loss: " << trainer.get_average_loss() << std::endl;
 		std::cout << "------------------------------------------------------------------" << std::endl << std::endl;
        
        DataLogStream << "Stop Code: " << stop_codes[stop] << std::endl;
        DataLogStream << "Final Average Loss: " << trainer.get_average_loss() << std::endl;
 		DataLogStream << "------------------------------------------------------------------" << std::endl << std::endl;       

//-----------------------------------------------------------------------------
//          TRAINING STOP
//-----------------------------------------------------------------------------
       
        // wait for training threads to stop
        trainer.get_net();
        stop_time = chrono::system_clock::now();

        elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);
        std::cout << "Training Complete.  Elapsed Time: " << elapsed_time.count() / 3600 << " hours" << std::endl;
        DataLogStream << "Training Complete.  Elapsed Time: " << elapsed_time.count() / 3600 << " hours" << std::endl;

        // Save the network to disk
        dfd_net.clean();
        dlib::serialize(sync_save_location + net_name) << dfd_net;

        ///////////////////////////////////////////////////////////////////////////////
        // Step 4: Show the training results
        ///////////////////////////////////////////////////////////////////////////////

        //dfd_net_type dfd_test_net;

        //std::cout << std::endl << "Loading " << (sync_save_location + net_name) << std::endl;
        //dlib::deserialize(sync_save_location + net_name) >> dfd_test_net;

        std::cout << "Analyzing Training Results..." << std::endl;

        train_results = eval_all_net_performance(dfd_net, tr, gt_train, ci.eval_crop_sizes, ci.scale);
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "NMAE, NRMSE, SSIM, Var_GT, Var_DM: " << std::fixed << std::setprecision(6) << train_results(0, 0) << ", " << train_results(0, 1) << ", " << train_results(0, 2) << ", " << train_results(0, 4) << ", " << train_results(0, 5) << std::endl;

        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream << "Training Image Analysis Results:" <<std::endl;
        DataLogStream << "NMAE, NRMSE, SSIM, Var_GT, Var_DM: " << std::fixed << std::setprecision(6) << train_results(0, 0) << ", " << train_results(0, 1) << ", " << train_results(0, 2) << ", " << train_results(0, 4) << ", " << train_results(0, 5) << std::endl;
        
        std::array<dlib::matrix<uint16_t>, img_depth> test_crop;
        dlib::matrix<uint16_t> map;
        dlib::matrix<double, 1, 6> tmp_results;


#ifndef DLIB_NO_GUI_SUPPORT

        dlib::image_window win0;
        dlib::image_window win1;
        dlib::image_window win2;

        std::cout << "Image Count: " << tr.size() << std::endl;

        for (idx = 0; idx < tr.size(); ++idx)
        {

            //center_cropper(tr[idx], test_crop, crop_sizes[1].second * scale.first, crop_sizes[1].first * scale.second);

            start_time = chrono::system_clock::now();
            tmp_results = eval_net_performance(dfd_net, tr[idx], gt_train[idx], map, ci.eval_crop_sizes, ci.scale);
            //dlib::matrix<uint16_t> map = dfd_test_net(test_crop);
            stop_time = chrono::system_clock::now();
            
            if(img_depth >= 3)
            {
                dlib::matrix<dlib::rgb_pixel> rgb_img;
                merge_channels(tr[idx], rgb_img, 0);

                win0.clear_overlay();
                win0.set_image(rgb_img);
                win0.set_title("Input Image");
            
                win1.clear_overlay();
                win1.set_image(mat_to_rgbjetmat(dlib::matrix_cast<float>(gt_train[idx]),0.0,255.0));
                win1.set_title("Ground Truth");

                win2.clear_overlay();
                win2.set_image(mat_to_rgbjetmat(dlib::matrix_cast<float>(map),0.0,255.0));
                win2.set_title("DNN Map");
            }
            
            elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);
            std::cout << "Image Crop #" << std::setw(5) << std::setfill('0') << idx << ": Elapsed Time: " << elapsed_time.count();
            std::cout << ", " << tmp_results(0,0) << ", " << tmp_results(0,1) << ", " << tmp_results(0,2) << std::endl;

        }

#endif

        ///////////////////////////////////////////////////////////////////////////////
        // Step 5: Run through test images
        ///////////////////////////////////////////////////////////////////////////////

        std::cout << std::endl << "Analyzing Test Results..." << std::endl;

        test_results = eval_all_net_performance(dfd_net, te, gt_test, ci.eval_crop_sizes, ci.scale);
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "NMAE, NRMSE, SSIM, Var_GT, Var_DM: " << std::fixed << std::setprecision(6) << test_results(0, 0) << ", " << test_results(0, 1) << ", " << test_results(0, 2) << ", " << test_results(0, 4) << ", " << test_results(0, 5) << std::endl;

        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream << "Test Image Analysis Results:" <<std::endl;
        DataLogStream << "NMAE, NRMSE, SSIM, Var_GT, Var_DM: " << std::fixed << std::setprecision(6) << test_results(0, 0) << ", " << test_results(0, 1) << ", " << test_results(0, 2) << ", " << test_results(0, 4) << ", " << test_results(0, 5) << std::endl;
 
        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream << std::setw(5) << std::setfill('0') << trainer.get_train_one_step_calls() << ", ";
        DataLogStream << std::fixed << std::setprecision(10) << trainer.get_learning_rate() << ", ";
        DataLogStream << std::fixed << std::setprecision(6) << trainer.get_average_loss() << ", ";

        DataLogStream << std::fixed << std::setprecision(6) << train_results(0, 0) << ", " << train_results(0, 1) << ", " << train_results(0, 2) << ", " << train_results(0, 4) << ", " << train_results(0, 5) << ", ";
        DataLogStream << std::fixed << std::setprecision(6) << test_results(0, 0) << ", " << test_results(0, 1) << ", " << test_results(0, 2) << ", " << test_results(0, 4) << ", " << test_results(0, 5) << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;


#ifndef DLIB_NO_GUI_SUPPORT

        std::cout << "Image Count: " << te.size() << std::endl;;

        for (idx = 0; idx < te.size(); ++idx)
        {
            //center_cropper(te[idx], test_crop, crop_sizes[1].second * scale.first, crop_sizes[1].first * scale.second);

            start_time = chrono::system_clock::now();
            tmp_results = eval_net_performance(dfd_net, te[idx], gt_test[idx], map, ci.eval_crop_sizes, ci.scale);
            //dlib::matrix<uint16_t> map = dfd_test_net(test_crop);
            stop_time = chrono::system_clock::now();

            if(img_depth >= 3)
            {
                dlib::matrix<dlib::rgb_pixel> rgb_img;
                merge_channels(te[idx], rgb_img, 0);

                win0.clear_overlay();
                win0.set_image(rgb_img);
                win0.set_title("Input Image");

                win1.clear_overlay();
                win1.set_image(mat_to_rgbjetmat(dlib::matrix_cast<float>(gt_test[idx]),0.0,255.0));
                win1.set_title("Groundtruth Depthmap");

                win2.clear_overlay();
                win2.set_image(mat_to_rgbjetmat(dlib::matrix_cast<float>(map),0.0,255.0));
                win2.set_title("DFD DNN Depthmap");
            }
            
            elapsed_time = chrono::duration_cast<d_sec>(stop_time - start_time);
            std::cout << "Image Crop #" << std::setw(5) << std::setfill('0') << idx << ": Elapsed Time: " << elapsed_time.count();
            std::cout << ", " << tmp_results(0,0) << ", " << tmp_results(0,1) << ", " << tmp_results(0,2) << std::endl;

        }

        win0.close_window();
        win1.close_window();
        win2.close_window();

#endif

        DataLogStream << std::endl;
        DataLogStream.close();

        std::cout << "End of Program." << std::endl;

    #if defined(_WIN32) | defined(__WIN32__) | defined(__WIN32) | defined(_WIN64) | defined(__WIN64)
        Beep(500, 1000);
        std::cout << "Press Enter to Close" << std::endl;
        std::cin.ignore();
    #endif

    }
    catch (std::exception& e)
    {
        std::cout << e.what() << std::endl;

        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream << e.what() << std::endl;
        DataLogStream << "------------------------------------------------------------------" << std::endl;
        DataLogStream.close();

        if(HPC == 0)
        {            
            std::cout << "Press Enter to Close" << std::endl;
            std::cin.ignore();
        }

    }

    return 0;

}    // end of main
