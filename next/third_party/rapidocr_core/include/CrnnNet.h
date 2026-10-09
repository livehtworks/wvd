#ifndef __OCR_CRNNNET_H__
#define __OCR_CRNNNET_H__

#include "OcrStruct.h"
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <span>

class CrnnNet {
public:

    ~CrnnNet();

    void setNumThread(int numOfThread);

    void setGpuIndex(int gpuIndex);

    void initModel(const std::string &pathStr, const std::string &keysPath);
    void cancel() { runOptions.SetTerminate(); }
    void resume() { runOptions.UnsetTerminate(); }

    std::vector<TextLine> getTextLines(std::vector<cv::Mat> &partImg, const char *path, const char *imgName);
    static TextLine decodeScores(const std::vector<float> &data, size_t h, size_t w,
                                 const std::vector<std::string> &dictionary);

private:
    bool isOutputDebugImg = false;
    Ort::Session *session{};
    Ort::RunOptions runOptions;
    Ort::SessionOptions sessionOptions = Ort::SessionOptions();
    int numThread = 0;

    std::vector<Ort::AllocatedStringPtr> inputNamesPtr;
    std::vector<Ort::AllocatedStringPtr> outputNamesPtr;

    const float meanValues[3] = {127.5, 127.5, 127.5};
    const float normValues[3] = {1.0 / 127.5, 1.0 / 127.5, 1.0 / 127.5};
    const int dstHeight = 48;

    std::vector<std::string> keys;

    static TextLine decodeView(std::span<const float> data, size_t h, size_t w,
                               const std::vector<std::string> &dictionary);

    TextLine getTextLine(const cv::Mat &src);
};


#endif //__OCR_CRNNNET_H__
