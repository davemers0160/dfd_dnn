#ifndef DFD_DNN_H_
#define DFD_DNN_H_

#include <cstdint>
#include <utility>

#include "file_parser.h"

extern const uint32_t img_depth;
extern const uint32_t secondary;

void parse_dnn_data_file(std::string parseFilename, std::string &version, std::vector<double> &stop_criteria, std::string &training_file, std::string &test_file, uint64_t &num_crops, std::vector<std::pair<uint64_t, uint64_t>> &crop_sizes, std::pair<uint32_t, uint32_t> &scale, std::vector<uint32_t> &filter_num)
{
    /*
    # Version 2.5
    # The file is organized in the following manner:
    # Version (std::string): version name for named svaing of various files
    # Stopping Criteria (double, double) [stop time (hrs), max one step]
    # training_file (std::string): This file contains a list of images and labels used for training
    # test_file (std::string): This file contains a list of images and labels used for testing
    # crop_num (uint64_t): The number of crops to use when using a random cropper
    # crop_size (uint64_t, uint64_t): This is the height and width of the crop size
    # filter_num (uint64_t...): This is the number of filters per layer.  Should be a comma separated list, eg. 10,20,30
    #             if the list does not account for the entire network then the code only uses what is available
    #             and leaves the remaining filter number whatever the default value was.  The order of the filters
    #             goes from outer most to the inner most layer.
    */

    std::vector<std::vector<std::string>> params;
    parse_csv_file(parseFilename, params);

    for (uint64_t idx = 0; idx<params.size(); ++idx)
    {
        switch (idx)
        {
        case 0:
            version = params[idx][0];
            break;
        case 1:
            try {

                stop_criteria.clear();
                for (uint64_t jdx = 0; jdx<params[idx].size(); ++jdx)
                {
                    stop_criteria.push_back(stod(params[idx][jdx]));
                }
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
                stop_criteria.push_back(160.0);
                stop_criteria.push_back(250000.0);
                std::cout << "Error getting stopping criteria.  Setting values to default." << std::endl;
            }
            break;
        case 2:
            training_file = params[idx][0];
            break;

        case 3:
            test_file = params[idx][0];
            break;

        case 4:
            try {
                num_crops = stol(params[idx][0]);
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
                num_crops = 2;
                std::cout << "Setting crop_num to " << num_crops << std::endl;
            }
            break;

        case 5:
            try {
                crop_sizes.push_back(std::make_pair(stol(params[idx][0]), stol(params[idx][1])));
                crop_sizes.push_back(std::make_pair(stol(params[idx][2]), stol(params[idx][3])));
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
                crop_sizes.push_back(std::make_pair(108, 36));
                crop_sizes.push_back(std::make_pair(108, 36));
                std::cout << "Setting Training Crop Size to " << crop_sizes[0].first << "x" << crop_sizes[0].second << std::endl;
                std::cout << "Setting Evaluation Crop Size to " << crop_sizes[1].first << "x" << crop_sizes[1].second << std::endl;
            }
            break;

        case 6:
            try {
                scale = std::make_pair(stol(params[idx][0]), stol(params[idx][1]));
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
                scale = std::make_pair(6, 18);
                std::cout << "Setting Scale to default: " << scale.first << " x " << scale.second << std::endl;
            }
            break;

        case 7:
            try {
                filter_num.clear();
                for (int jdx = 0; jdx<params[idx].size(); ++jdx)
                {
                    filter_num.push_back(stol(params[idx][jdx]));
                }
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
                filter_num.clear();

                std::cout << "Error getting filter numbers.  No values passed on." << std::endl;
            }
            break;

        default:
            break;
        }   // end of switch

    }   // end of for

}   // end of parse_dnn_data_file

#endif  // DFD_DNN_H_
