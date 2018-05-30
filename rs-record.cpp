// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include <iostream>
#include <vector>
// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API
#include <opencv2/highgui.hpp>

int main(int argc, char * argv[]) try
{
    // Declare depth colorizer for pretty visualization of depth data
    rs2::colorizer color_map;

    // Declare RealSense pipeline, encapsulating the actual device and sensors
    rs2::pipeline pipe;
    // Start streaming with default recommended configuration
    pipe.start();

    using namespace cv;
    const auto window_name = "Display Image";
    namedWindow(window_name, WINDOW_AUTOSIZE);

#if 1
    int ret;
    if (argc < 2) {
        std::cout << "Usage: rs-record <outfile>" << std::endl;
        return 1;
    }
    const char* outfile = argv[1];

    // initialize FFmpeg library
    av_register_all();

    // one time call to help init
    rs2::frameset data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
    rs2::frame color_frame = data.get_color_frame();
    // Query frame size (width and height)
    const int dst_width = color_frame.as<rs2::video_frame>().get_width();
    const int dst_height = color_frame.as<rs2::video_frame>().get_height();
    const AVRational dst_fps = {30, 1};

    // open output format context
    AVFormatContext* outctx = nullptr;
    ret = avformat_alloc_output_context2(&outctx, nullptr, nullptr, outfile);
    if (ret < 0) {
        std::cerr << "fail to avformat_alloc_output_context2(" << outfile << "): ret=" << ret;
        std::cerr << "fail to avformat_alloc_output_context2(" << outfile << "): ret=" << ret;
        return 2;
    }

    // open output IO context
    ret = avio_open2(&outctx->pb, outfile, AVIO_FLAG_WRITE, nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "fail to avio_open2: ret=" << ret;
        return 2;
    }

    // create new video stream
    AVCodec* vcodec = avcodec_find_encoder(outctx->oformat->video_codec);
    AVStream* vstrm = avformat_new_stream(outctx, vcodec);
    if (!vstrm) {
        std::cerr << "fail to avformat_new_stream";
        return 2;
    }
    avcodec_get_context_defaults3(vstrm->codec, vcodec);
    vstrm->codec->width = dst_width;
    vstrm->codec->height = dst_height;
    vstrm->codec->pix_fmt = vcodec->pix_fmts[0];
    vstrm->codec->time_base = vstrm->time_base = av_inv_q(dst_fps);
    vstrm->r_frame_rate = vstrm->avg_frame_rate = dst_fps;
    if (outctx->oformat->flags & AVFMT_GLOBALHEADER)
        vstrm->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // open video encoder
    ret = avcodec_open2(vstrm->codec, vcodec, nullptr);
    if (ret < 0) {
        std::cerr << "fail to avcodec_open2: ret=" << ret;
        return 2;
    }

    std::cout
        << "outfile: " << outfile << "\n"
        << "format:  " << outctx->oformat->name << "\n"
        << "vcodec:  " << vcodec->name << "\n"
        << "size:    " << dst_width << 'x' << dst_height << "\n"
        << "fps:     " << av_q2d(dst_fps) << "\n"
        << "pixfmt:  " << av_get_pix_fmt_name(vstrm->codec->pix_fmt) << "\n"
        << std::flush;

    // initialize sample scaler
    SwsContext* swsctx = sws_getCachedContext(
        nullptr, dst_width, dst_height, AV_PIX_FMT_BGR24,
        dst_width, dst_height, vstrm->codec->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsctx) {
        std::cerr << "fail to sws_getCachedContext";
        return 2;
    }

    // allocate frame buffer for encoding
    AVFrame* frame = av_frame_alloc();
    std::vector<uint8_t> framebuf(avpicture_get_size(vstrm->codec->pix_fmt, dst_width, dst_height));
    avpicture_fill(reinterpret_cast<AVPicture*>(frame), framebuf.data(), vstrm->codec->pix_fmt, dst_width, dst_height);
    frame->width = dst_width;
    frame->height = dst_height;
    frame->format = static_cast<int>(vstrm->codec->pix_fmt);

    avformat_write_header(outctx, nullptr);
    int64_t frame_pts = 0;
    unsigned nb_frames = 0;
    bool end_of_stream = false;
    int got_pkt = 0;

#endif


    //while ((waitKey(1) < 0 && cvGetWindowHandle(window_name)))
    do
    {
        rs2::frameset data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
        rs2::frame color_frame = data.get_color_frame();

        // Query frame size (width and height)
        const int w = color_frame.as<rs2::video_frame>().get_width();
        const int h = color_frame.as<rs2::video_frame>().get_height();

        // Create OpenCV matrix of size (w,h) from the colorized depth data
        Mat image(Size(w, h), CV_8UC3, (void*)color_frame.get_data(), Mat::AUTO_STEP);


#if 1

        if (!end_of_stream) {
            // Update the window with new data
            cv::cvtColor(image, image, cv::COLOR_RGB2BGR);
            if(cvGetWindowHandle(window_name))
                imshow(window_name, image);

            if (cv::waitKey(33) == 0x1b)
                end_of_stream = true;
        }

        if (!end_of_stream) {
            // convert cv::Mat(OpenCV) to AVFrame(FFmpeg)
            const int stride[] = { static_cast<int>(image.step[0]) };
            sws_scale(swsctx, &image.data, stride, 0, image.rows, frame->data, frame->linesize);
            frame->pts = frame_pts++;
        }

        // encode video frame
        AVPacket pkt;
        pkt.data = nullptr;
        pkt.size = 0;
        av_init_packet(&pkt);
        ret = avcodec_encode_video2(vstrm->codec, &pkt, end_of_stream ? nullptr : frame, &got_pkt);
        if (ret < 0) {
            std::cerr << "fail to avcodec_encode_video2: ret=" << ret << "\n";
            break;
        }
        if (got_pkt) {
            // rescale packet timestamp
            pkt.duration = 1;
            av_packet_rescale_ts(&pkt, vstrm->codec->time_base, vstrm->time_base);
            // write packet
            av_write_frame(outctx, &pkt);
            std::cout << nb_frames << " , and time base is: "  << '\r' << std::flush;  // dump progress
            ++nb_frames;
        }
        av_free_packet(&pkt);
#endif
    //}
    } while (!end_of_stream || got_pkt);


    // End of Stream?

#if 1
    av_write_trailer(outctx);
    std::cout << nb_frames << " frames encoded" << std::endl;

    av_frame_free(&frame);
    avcodec_close(vstrm->codec);
    avio_close(outctx->pb);
    avformat_free_context(outctx);

#endif

    return EXIT_SUCCESS;
}
catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}



