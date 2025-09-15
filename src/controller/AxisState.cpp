#include "controller/AxisState.h"

void AxisState::updatePosition(int axis_no, int position) {
    std::lock_guard<std::mutex> lock(mtx_);
    axes_data_[axis_no].position = position;
    axes_data_[axis_no].last_updated_time = std::chrono::steady_clock::now();
}

void AxisState::updateStatus(int axis_no, const std::vector<std::string>& params) {
    if (params.size() < 6) return; // Ensure all parameters are present

    std::lock_guard<std::mutex> lock(mtx_);
    try {
        axes_data_[axis_no].status.driving_state = std::stoi(params[0]);
        axes_data_[axis_no].status.emg_signal = std::stoi(params[1]);
        axes_data_[axis_no].status.org_norg_signal = std::stoi(params[2]);
        axes_data_[axis_no].status.cw_ccw_limit_signal = std::stoi(params[3]);
        axes_data_[axis_no].status.soft_limit_state = std::stoi(params[4]);
        axes_data_[axis_no].status.correction_allowable_range = std::stoi(params[5]);
        axes_data_[axis_no].last_updated_time = std::chrono::steady_clock::now();
    } catch (...) {
        // Handle potential parsing errors
    }
}

int AxisState::getPosition(int axis_no) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = axes_data_.find(axis_no);
    if (it != axes_data_.end()) {
        return it->second.position;
    }
    return 0; // Default value if axis not found
}

AxisStatus AxisState::getStatusDetails(int axis_no) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = axes_data_.find(axis_no);
    if (it != axes_data_.end()) {
        return it->second.status;
    }
    return AxisStatus(); // Return a default constructed status
}
