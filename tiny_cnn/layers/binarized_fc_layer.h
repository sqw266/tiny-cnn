#pragma once
#include "tiny_cnn/layers/layer.h"
#include "tiny_cnn/util/product.h"
#include <vector>

namespace tiny_cnn {

template<typename Activation>
class binarized_fc_layer : public layer<Activation> {
public:
    typedef layer<Activation> Base;
    CNN_USE_LAYER_MEMBERS;

    binarized_fc_layer(cnn_size_t in_dim, cnn_size_t out_dim)
        : Base(in_dim, out_dim, size_t(in_dim) * out_dim, 0) {
        // initialize all binarized weights to false
        for(unsigned int i = 0; i < connection_size(); i++)
            Wbin_.push_back(false);
    }

    size_t connection_size() const override {
        return size_t(in_size_) * out_size_;
    }

    size_t fan_in_size() const override {
        return in_size_;
    }

    size_t fan_out_size() const override {
        return out_size_;
    }

    virtual void post_update() {
        // once the weights have been updated, update the binarized versions too
        float2bipolar(W_, Wbin_);
    }

    const vec_t& forward_propagation(const vec_t& in, size_t index) override {
        std::vector<bool> in_bin(in_size_, false);
        // explicitly binarize the input
        float2bipolar(in, in_bin);
        vec_t &a = a_[index];
        vec_t &out = output_[index];

        for_i(parallelize_, out_size_, [&](int i) {
            a[i] = float_t(0);
            for (cnn_size_t c = 0; c < in_size_; c++) {
                // multiplication for binarized values is basically XNOR (equals)
                // i.e. if two values have the same sign (pos-pos or neg-neg)
                // the mul. result will be positive, otherwise negative
                a[i]  += (Wbin_[c*out_size_ + i] == in_bin[c]) ? +1 : -1;
            }
        });

        for_i(parallelize_, out_size_, [&](int i) {
            out[i] = h_.f(a, i);
        });
        CNN_LOG_VECTOR(out, "[bfc]forward");

        return next_ ? next_->forward_propagation(out, index) : out;
    }

    const vec_t& back_propagation(const vec_t& curr_delta, size_t index) override {
        throw "Not yet implemented";
        return curr_delta;
    }

    const vec_t& back_propagation_2nd(const vec_t& current_delta2) override {
        throw "Not yet implemented";
        return current_delta2;
    }

    std::string layer_type() const override { return "binarized-fully-connected"; }

protected:
    std::vector<bool> Wbin_;

    // utility function to convert a vector of floats into a vector of bools, where the
    // output boolean represents the sign of the input value (false: negative,
    // true: positive)
    void float2bipolar(const vec_t & in, std::vector<bool> & out) {
        for(unsigned int i = 0; i < in.size(); i++)
            out[i] = in[i] >= 0 ? true : false;
    }

};

} // namespace tiny_cnn
