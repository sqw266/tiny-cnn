/*
    Copyright (c) 2016, Xilinx, Inc.
    All rights reserved.

    Use of this source code is governed by a BSD-style license that can be found
    in the LICENSE file.
*/
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include "tiny_dnn/layers/layer.h"
#include "tiny_dnn/util/product.h"

namespace tiny_dnn {

class bnn_conv_layer : public layer {
 public:
  typedef layer Base;

  // assumptions: padding::valid, wstride=hstride=1, no bias
  bnn_conv_layer(serial_size_t in_width,
                 serial_size_t in_height,
                 serial_size_t window_size,
                 serial_size_t in_channels,
                 serial_size_t out_channels,
                 bool use_popcount           = false,
                 std::string binaryParamFile = "")
    : Base(in_width * in_height * in_channels,
           (in_width - window_size + 1) * (in_height - window_size + 1) *
             out_channels,
           out_channels * in_channels * window_size * window_size,
           0),
      in_width_(in_width),
      in_height_(in_height),
      window_size_(window_size),
      in_channels_(in_channels),
      out_channels_(out_channels),
      Wbin_(out_channels * in_channels * window_size * window_size, false),
      use_popcount_(use_popcount) {
    // TODO re-enable parallelization -- need to support worker index in forward
    // prop
    Base::set_parallelize(false);
    out_width_  = (in_width - window_size + 1);
    out_height_ = (in_height - window_size + 1);

    if (binaryParamFile != "") loadFromBinaryFile(binaryParamFile);
  }

  void loadFromBinaryFile(std::string fileName) {
    // TODO this assumes the binary file always uses 8 bytes per threshold entry

    // load weights
    std::ifstream wf(fileName, std::ios::binary | std::ios::in);
    if (!wf.is_open()) throw "Could not open file";
    for (size_t line = 0; line < Wbin_.size(); line++) {
      unsigned long long e = 0;
      wf.read((char*)&e, sizeof(unsigned long long));  // TODO
      Wbin_[line] = e == 1;
    }
    wf.close();
  }

  ///< number of incoming connections for each output unit
  virtual size_t fan_in_size() const override {
    return in_channels_ * window_size_ * window_size_;
  }

  ///< number of outgoing connections for each input unit
  virtual size_t fan_out_size() const override {
    return out_channels_ * window_size_ * window_size_;
  }

  ///< number of connections
  virtual size_t connection_size() const override {
    return out_height_ * out_width_ * fan_in_size();
  }

  std::string layer_type() const override { return "bnn_conv_layer"; }

  virtual void post_update() {
    // once the weights have been updated, update the binarized versions too
    float2bipolar(W_, Wbin_);
  }

  virtual const vec_t& back_propagation_2nd(
    const vec_t& current_delta2) override {
    throw "Not implemented";
  }

  virtual const vec_t& forward_propagation(const vec_t& in_raw,
                                           size_t worker_index) override {
    // turn the input into a vector of bools
    std::vector<bool> in_bin(in_raw.size(), false);
    float2bipolar(in_raw, in_bin);
    vec_t& out = output_[worker_index];

    // TODO implement actual binarized version
    // TODO support padding modes
    // TODO support worker index for parallelization
    for (serial_size_t oc = 0; oc < out_channels_; oc++) {
      size_t output_base = oc * out_height_ * out_width_;
      for (serial_size_t oy = 0; oy < out_height_; oy++) {
        for (serial_size_t ox = 0; ox < out_width_; ox++) {
          int acc = 0;
          for (serial_size_t ic = 0; ic < in_channels_; ic++) {
            size_t weight_base =
              oc * (window_size_ * window_size_ * in_channels_) +
              (window_size_ * window_size_ * ic);
            size_t input_base =
              ic * (in_width_ * in_height_) + oy * in_width_ + ox;
            for (serial_size_t ky = 0; ky < window_size_; ky++) {
              for (serial_size_t kx = 0; kx < window_size_; kx++) {
                size_t weight_ind = weight_base + ky * window_size_ + kx;
                size_t input_ind  = input_base + ky * in_width_ + kx;
                if (use_popcount_) {
                  // accumulate popcount (+1 bits) only
                  acc += Wbin_[weight_ind] == in_bin[input_ind] ? +1 : 0;
                } else {
                  // accumulate sum of +1 and -1s
                  acc += Wbin_[weight_ind] == in_bin[input_ind] ? +1 : -1;
                }
              }
            }
          }
          size_t output_ind = output_base + oy * out_width_ + ox;
          out[output_ind]   = acc;
        }
      }
    }

    CNN_LOG_VECTOR(out, "[bnn_conv_layer] forward ");

    return next_ ? next_->forward_propagation(out, worker_index) : out;
  }

  const vec_t& back_propagation(const vec_t& curr_delta,
                                size_t index) override {
    throw "Not implemented";
  }

 protected:
  bool use_popcount_;
  std::vector<bool> Wbin_;
  serial_size_t in_width_;
  serial_size_t in_height_;
  serial_size_t window_size_;
  serial_size_t in_channels_;
  serial_size_t out_channels_;
  serial_size_t out_width_;
  serial_size_t out_height_;

  /**
   * utility function to convert a vector of floats into a vector of bools
   * @param in a vector of floats
   * @param out a vector of bools where the output boolean represents the sign
   * of the input value (false: negative, true: positive)
   */
  void float2bipolar(const vec_t& in, std::vector<bool>& out) {
    for (size_t i = 0; i < in.size(); i++) out[i] = in[i] >= 0;
  }
};
}
