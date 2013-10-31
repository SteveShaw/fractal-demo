#include <map>
#include <chrono>
#include <time.h>
#include <vector>
#include <cstdlib>
#include <iostream>

#include <QImage>
#include <QColor>
#include <QBuffer>
#include <QByteArray>

#include "cppa/opt.hpp"
#include "cppa/cppa.hpp"

#include "client.hpp"
#include "config.hpp"
#include "fractal_request.hpp"
#include "calculate_fractal.hpp"
#include "q_byte_array_info.hpp"

#ifdef ENABLE_OPENCL
#include "cppa/opencl.hpp"
#endif // ENABLE_OPENCL

using namespace std;
using namespace cppa;

any_tuple response_from_image(QImage image, uint32_t image_id, bool is_opencl_enabled) {
    QByteArray ba;
    QBuffer buf{&ba};
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, image_format);
    buf.close();
    // last argument identifies this worker as a "normal" actor
    return make_any_tuple(atom("result"), image_id, std::move(ba), is_opencl_enabled);
}

#ifdef ENABLE_OPENCL

namespace {

constexpr const char* kernel_source = R"__(
    __kernel void mandelbrot(__global float* config,
                             __global int* output)
    {
        unsigned iterations = config[0];
        unsigned width = config[1];
        unsigned height = config[2];

        float min_re = config[3];
        float max_re = config[4];
        float min_im = config[5];
        float max_im = config[6];

        float re_factor = (max_re-min_re)/(width-1);
        float im_factor = (max_im-min_im)/(height-1);

        unsigned x = get_global_id(0);
        unsigned y = get_global_id(1);
        float z_re = min_re + x*re_factor;
        float z_im = max_im - y*im_factor;
        float const_re = z_re;
        float const_im = z_im;
        unsigned cnt = 0;
        float cond = 0;
        do {
            float tmp_re = z_re;
            float tmp_im = z_im;
            z_re = ( tmp_re*tmp_re - tmp_im*tmp_im ) + const_re;
            z_im = ( 2 * tmp_re * tmp_im ) + const_im;
            cond = z_re*z_re + z_im*z_im;
            cnt ++;
        } while (cnt < iterations && cond <= 4.0f);
        output[x+y*width] = cnt;
    }
)__";

} // namespace <anonymous>

using palette_ptr = shared_ptr<vector<QColor>>;

class clbroker : public event_based_actor {

 public:

    void init() {
        become (
            on(atom("assign"), arg_match)
            >> [=](uint32_t width, uint32_t height, uint32_t iterations, uint32_t image_id, float_type min_re, float_type max_re, float_type min_im, float_type max_im) {
//                cout << "opencl worker received work" << endl;
                m_current_server = self->last_sender();
                if (   width != clwidth
                    || height != clheight
                    || iterations != cliterations) {
                    calculate_palette(palette, iterations);
                    clworker = spawn_cl<int*(float*)>(clprog, "mandelbrot",
                                                      {width, height});
                    clwidth = width;
                    clheight = height;
                    cliterations = iterations;
                }
                clforward(image_id, min_re, max_re, min_im, max_im);
            },
            others() >> [=] {
                aout << "Unexpected message: '"
                     << to_string(self->last_dequeued()) << "'.\n";
            }
        );
    }

    clbroker(uint32_t device_id)
    : clprog(opencl::program::create(kernel_source, device_id)), clwidth(0), clheight(0), cliterations(0) { }

 private:

    void clforward(uint32_t image_id, float_type min_re, float_type max_re, float_type min_im, float_type max_im) {
        m_current_server = self->last_sender();
        vector<float> cljob;
        cljob.reserve(7);
        cljob.push_back(cliterations);
        cljob.push_back(clwidth);
        cljob.push_back(clheight);
        cljob.push_back(min_re);
        cljob.push_back(max_re);
        cljob.push_back(min_im);
        cljob.push_back(max_im);
        auto hdl = self->make_response_handle();
        sync_send(clworker, std::move(cljob)).then (
            on_arg_match >> [=](const vector<int>& result) {
                QImage image{static_cast<int>(clwidth),
                             static_cast<int>(clheight),
                             QImage::Format_RGB32};
                for (uint32_t y = 0; y < clheight; ++y) {
                    for (uint32_t x = 0; x < clwidth; ++x) {
                        image.setPixel(x,y,palette[result[x+y*clwidth]].rgb());
                    }
                }
                // last argument identifies this worker as an opencl-enabled actor
                reply_tuple_to(hdl, response_from_image(image, image_id, true));
            }
        );
    }

    opencl::program clprog;
    uint32_t clwidth;
    uint32_t clheight;
    uint32_t cliterations;
    actor_ptr clworker;
    actor_ptr m_current_server;
    std::vector<QColor> palette;

};

//void clbroker(uint32_t clwidth, uint32_t clheight, uint32_t cliterations, actor_ptr clworker, palette_ptr palette) {
//}

actor_ptr spawn_opencl_client(uint32_t device_id) {
    return spawn<clbroker>(device_id);
}

#else

cppa::actor_ptr spawn_opencl_client(uint32_t) {
    throw std::logic_error("spawn_opencl_client: compiled wo/ OpenCL");
}

#endif // ENABLE_OPENCL

void client::init() {
    become (
        on(atom("quit")) >> [=] {
            quit();
        },
        on(atom("assign"), arg_match) >> [=](uint32_t width,
                                             uint32_t height,
                                             uint32_t iterations,
                                             uint32_t image_id,
                                             float_type min_re,
                                             float_type max_re,
                                             float_type min_im,
                                             float_type max_im) {
//            cout << "normal worker received work" << endl;
            m_current_server = self->last_sender();
            // was reply_tuple
            return (
                response_from_image(
                    calculate_fractal(m_palette, width, height, iterations,
                                      min_re, max_re, min_im, max_im),
                    image_id,
                    false
                )
            );
        },
        others() >> [=] {
            aout << "Unexpected message: '"
                 << to_string(last_dequeued()) << "'.\n";
        }
    );
}
