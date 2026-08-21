#ifndef CLBLAST_TUNING_JSON_LOGGING_H_
#define CLBLAST_TUNING_JSON_LOGGING_H_

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "tuning/configurations.hpp"
#include "tuning/tuning.hpp"
#include "utilities/backend.hpp"
#include "utilities/utilities.hpp"

/**
 * best_kernel
 * best_time
 * best_parameters
 */

namespace clblast {
class JSONLogger {
 public:
  JSONLogger(const std::string& filename, const Device& device, const Platform& platform,
             const std::vector<std::pair<std::string, std::string>>& metadata, bool resume) {
    nlohmann::ordered_json old_json;

    if (!resume) {
      file_ = std::fstream(filename, std::ios::out | std::ios::trunc | std::ios::in);
    } else {
      file_ = std::fstream(filename, std::ios::out | std::ios::in);
      old_json = nlohmann::ordered_json::parse(file_);
    }

    nlohmann::ordered_json json;

    for (auto& datum : metadata) {
      json[datum.first.c_str()] = datum.second.c_str();
    }
    json["clblast_device_type"] = GetDeviceType(device).c_str();
    json["clblast_device_vendor"] = GetDeviceVendor(device).c_str();
    json["clblast_device_architecture"] = GetDeviceArchitecture(device).c_str();
    json["clblast_device_name"] = GetDeviceName(device).c_str();
    json["device"] = device.Name().c_str();
    json["platform_vendor"] = platform.Vendor().c_str();
    json["platform_version"] = platform.Version().c_str();
    json["device_vendor"] = device.Vendor().c_str();
    json["device_type"] = device.Type().c_str();
    json["device_core_clock"] = device.CoreClock();
    json["device_compute_units"] = device.ComputeUnits();
    json["device_extra_info"] = device.GetExtraInfo().c_str();
    json["results"] = nlohmann::json::array();

    resume = true;

    try {
      if (resume && !old_json.empty() && json["clblast_device_type"] == old_json.at("clblast_device_type") &&
          json["clblast_device_vendor"] == old_json.at("clblast_device_vendor") &&
          json["clblast_device_architecture"] == old_json.at("clblast_device_architecture") &&
          json["clblast_device_name"] == old_json.at("clblast_device_name") &&
          json["device"] == old_json.at("device") && json["platform_vendor"] == old_json.at("platform_vendor") &&
          json["platform_version"] == old_json.at("platform_version") &&
          json["device_vendor"] == old_json.at("device_vendor") && json["device_type"] == old_json.at("device_type") &&
          json["device_core_clock"] == old_json.at("device_core_clock") &&
          json["device_compute_units"] == old_json.at("device_compute_units") &&
          json["device_extra_info"] == old_json.at("device_extra_info")) {
        printf("* Resuming from previous tuner run\n");

        file_.close();
        file_ = std::fstream(filename, std::ios::out | std::ios::trunc | std::ios::in);
        has_result_ = true;

        if (old_json.contains("results")) {
          for (const auto& result : old_json["results"]) {
            Configuration config;
            for (const auto& param : result["parameters"].items()) {
              config[param.key()] = param.value();
            }
            configs_.insert(config);
          }
        } else {
          old_json["results"] = nlohmann::json::array();
        }

        file_ << old_json.dump();  // Remove pretty-print for compatibility below
        file_.flush();
      } else {
        if (resume) {
          printf(
              "* WARNING: Currently loaded JSON file does not match the current device and platform. Overwriting the "
              "existing file.\n");
        }
        file_.seekp(0, std::ios::beg);
        file_.clear();
        file_.seekp(0, std::ios::beg);

        file_ << json.dump();
        file_.flush();
      }
    } catch (nlohmann::json::out_of_range&) {
      printf("* WARNING: Invalid JSON file found. Overwriting the existing file.\n");

      file_.seekp(0, std::ios::beg);
      file_.clear();
      file_.seekp(0, std::ios::beg);

      file_ << json.dump();
      file_.flush();
    }
  }

  void remove_existing_configs(std::vector<Configuration>& configs) {
    const auto remove_el = std::remove_if(configs.begin(), configs.end(),
                                          [&](const Configuration& c) { return configs_.find(c) != configs_.end(); });
    if (remove_el != configs.end()) {
      printf("* Removing %zu configuration(s) that have already been tested\n",
             std::distance(remove_el, configs.end()));
      configs.erase(remove_el, configs.end());
    }
  }

  void add_tuning_result(const TuningResult& result) {
    if (!has_result_) {
      file_.seekp(-(sizeof("]}") - 1), std::ios::end);
      has_result_ = true;
    } else {
      file_.seekp(-(sizeof("]}") - 1), std::ios::end);
      file_ << ',';
    }

    nlohmann::ordered_json result_json;

    result_json["kernel"] = result.name.c_str();
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << result.score;
    result_json["time"] = std::stod(stream.str());  // Add %.3lf

    // Loops over all the parameters for this result
    result_json["parameters"] = nlohmann::ordered_json::object();
    for (const auto& parameter : result.config) {
      result_json["parameters"][parameter.first.c_str()] = parameter.second;
    }

    auto s = result_json.dump() + "]}";

    file_ << s;

    file_.flush();
  }

  void update_best(const std::pair<std::string, std::string>& best_kernel,
                   const std::pair<std::string, std::string>& best_time,
                   const std::pair<std::string, std::string>& best_parameters) {
    file_.seekp(0, std::ios::beg);
    auto json = nlohmann::ordered_json::parse(file_);
    json["best_kernel"] = best_kernel.second;
    json["best_time"] = best_time.second;
    json["best_parameters"] = best_parameters.second;

    file_.clear();
    file_.seekp(0, std::ios::beg);

    file_ << json.dump(2);  // Pretty-print the file
    file_.flush();
  }

  ~JSONLogger() { file_.close(); }

 private:
  bool has_result_ = false;
  std::fstream file_;
  std::set<Configuration> configs_;
};
}  // namespace clblast

// CLBLAST_TUNING_JSON_LOGGING_H_
#endif