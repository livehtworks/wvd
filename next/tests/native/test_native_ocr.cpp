#include "recognition/ocr.hpp"
#include "CrnnNet.h"
#include "OcrLite.h"
#include "OcrUtils.h"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--run-model-contract") {
            using namespace wvd::recognition;
            const auto root = std::filesystem::path(argv[2]);
            cv::Mat image(180,600,CV_8UC3,cv::Scalar{255,255,255});
            cv::putText(image,"NEXT",{45,125},cv::FONT_HERSHEY_SIMPLEX,2.8,cv::Scalar{0,0,0},5);
            unsigned created{};
            std::weak_ptr<OcrEngine> weak;
            std::vector<wvd::contracts::RecognitionMatch> first;
            {
                RunOcrModels pool;
                auto factory = [&] { ++created; return std::make_shared<OcrEngine>(root); };
                auto model = pool.acquire(0,"locked-model",factory);
                weak = model;
                first = model->recognize(image);
                if (first.empty()) throw std::runtime_error("OCR_POOL_INFERENCE_MISSING");
                bool active_rejected{};
                try { (void)pool.acquire(0,"locked-model",factory); }
                catch (const std::exception &e) { active_rejected = std::string(e.what()) == "OCR_PREVIOUS_SERVICE_LEASE_ACTIVE"; }
                if (!active_rejected) throw std::runtime_error("OCR_ACTIVE_LEASE_RESET");
                model->cancel(); model.reset();
                model = pool.acquire(0,"locked-model",factory);
                const auto second = model->recognize(image);
                if (created != 1 || first.size() != second.size()) throw std::runtime_error("OCR_POOL_NOT_REUSED");
                for (std::size_t i=0; i<first.size(); ++i)
                    if(first[i].text != second[i].text || first[i].score != second[i].score)
                        throw std::runtime_error("OCR_REUSED_MODEL_DIFFERENT");
                model.reset();
                bool identity_rejected{};
                try { (void)pool.acquire(0,"different-model",factory); }
                catch (const std::exception &e) { identity_rejected = std::string(e.what()) == "OCR_RUN_MODEL_IDENTITY_CHANGED"; }
                if (!identity_rejected) throw std::runtime_error("OCR_MODEL_IDENTITY_REPLACED");
            }
            if (!weak.expired()) throw std::runtime_error("OCR_MODEL_SURVIVED_RUN");
            std::cout << "PASS actual ORT model: run-owned reuse, active lease protected, cancel reset only after release, identity pinned, run destruction\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--result-contract") {
            const auto root = std::filesystem::path(argv[2]);
            cv::Mat image(500,900,CV_8UC3,cv::Scalar{255,255,255});
            cv::putText(image,"NEXT",{80,170},cv::FONT_HERSHEY_SIMPLEX,2.8,cv::Scalar{0,0,0},5);
            cv::putText(image,"READY",{280,390},cv::FONT_HERSHEY_SIMPLEX,2.8,cv::Scalar{0,0,0},5);
            const auto original = image.clone();
            OcrLite model; model.setNumThread(2); model.initLogger(false,false,false);
            if (!model.initModels((root/"det.onnx").string(),{},(root/"rec.onnx").string(),(root/"keys.txt").string()))
                throw std::runtime_error("OCR_MODEL_INITIALIZATION_FAILED");
            const auto full = model.detect(image,0,900,.6f,.3f,1.5f,false,false);
            model.setResultOnly(true);
            const auto quiet = model.detect(image,0,900,.6f,.3f,1.5f,false,false);
            if (full.textBlocks.size()<2 || full.textBlocks.size()!=quiet.textBlocks.size() ||
                full.boxImg.empty() || !quiet.boxImg.empty() || !quiet.strRes.empty())
                throw std::runtime_error("OCR_RESULT_ONLY_CONTRACT");
            for (std::size_t i=0;i<full.textBlocks.size();++i) {
                const auto &a=full.textBlocks[i], &b=quiet.textBlocks[i];
                if(a.text!=b.text || a.charScores!=b.charScores || a.boxPoint!=b.boxPoint || a.boxScore!=b.boxScore)
                    throw std::runtime_error("OCR_RESULT_DIFFERENT");
            }
            for (const auto &box : std::vector<std::vector<cv::Point>>{
                    {{0,0},{120,0},{120,80},{0,80}},{{20,40},{150,20},{160,90},{30,110}},
                    {{770,390},{900,390},{900,500},{770,500}},{{40,20},{65,20},{65,180},{40,180}}}) {
                const auto actual=getRotateCropImage(image,box);
                // The historical helper first copied the complete source; an
                // independently allocated image must produce identical pixels.
                const auto expected=getRotateCropImage(original.clone(),box);
                if(actual.size()!=expected.size() || cv::norm(actual,expected,cv::NORM_INF)!=0)
                    throw std::runtime_error("OCR_CROP_PIXELS_DIFFERENT");
            }
            bool invalid=false;
            try { (void)getRotateCropImage(image,{}); } catch(const std::exception &) { invalid=true; }
            if(!invalid || cv::norm(image,original,cv::NORM_INF)!=0) throw std::runtime_error("OCR_SOURCE_MODIFIED");
            std::cout << "PASS result-only OCR: multiple boxes, equal text/scores/geometry, no result image, unchanged input\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--ctc-contract") {
            const std::vector<std::string> keys{"#","A"," "};
            auto decoded = CrnnNet::decodeScores({.1f,.2f,.9f},1,3,keys);
            if (decoded.text != " " || decoded.charScores.size()!=1 || decoded.charScores[0]!=.9f)
                throw std::runtime_error("CTC_FINAL_CLASS_SKIPPED");
            decoded=CrnnNet::decodeScores({.1f,.9f,.2f,.1f,.9f,.2f,.9f,.1f,.2f,.1f,.9f,.2f},4,3,keys);
            if (decoded.text!="AA") throw std::runtime_error("CTC_BLANK_REPEAT_CONTRACT");
            bool rejected=false;
            try { (void)CrnnNet::decodeScores({.1f,.2f},1,3,keys); } catch (const std::exception &) { rejected=true; }
            if (!rejected) throw std::runtime_error("CTC_INVALID_SHAPE_ACCEPTED");
            std::cout << "PASS CTC last class, last step, duplicate/blank, shape contract\n";
            return 0;
        }
        const bool real_frame = argc == 4 && std::string(argv[2]) == "--real-memory-frame";
        if (argc != 2 && argc != 3 && !real_frame) throw std::runtime_error("OCR_MODEL_PATH_REQUIRED");
        const bool memory_cycles = real_frame || (argc == 3 && std::string(argv[2]) == "--memory-cycles");
        if (argc == 3 && !memory_cycles) throw std::runtime_error("OCR_OPTION_UNKNOWN");
        const auto root = std::filesystem::path(argv[1]);
        wvd::platform::BundleLease::Manifest manifest;
        for (const auto &entry : std::filesystem::directory_iterator(root))
            if (entry.is_regular_file()) manifest.emplace(entry.path().filename().string(),
                wvd::platform::file_sha256(entry.path()));
        wvd::platform::BundleLease lease(root, "published-model-lock-check", manifest);
        for (int cycle = 0; cycle < (memory_cycles ? 4 : 1); ++cycle) {
        const auto before = wvd::platform::sample_memory();
        {
        wvd::recognition::OcrEngine engine{root};
        if (real_frame) {
            const auto image = cv::imread(argv[3]);
            if (image.size() != cv::Size(900, 1600)) throw std::runtime_error("REAL_FRAME_INVALID");
            // Reuse real pixels at the production skill/modal/card ROI sizes.
            for (const cv::Rect roi : {cv::Rect{0,600,900,1000}, {0,1150,900,450}, {0,930,450,180}}) {
                const auto matches = engine.recognize(image(roi));
                std::cout << "real_roi=" << roi << " matches=" << matches.size() << '\n';
                if (matches.empty()) throw std::runtime_error("REAL_OCR_MATCHES_MISSING");
            }
        } else {
        cv::Mat image(180, 600, CV_8UC3, cv::Scalar(255, 255, 255));
        cv::putText(image, "NEXT", {45, 125}, cv::FONT_HERSHEY_SIMPLEX,
                    2.8, cv::Scalar(0, 0, 0), 5, cv::LINE_AA);
        const auto matches = engine.recognize(image);
        bool found{};
        for (const auto &match : matches) {
            std::cout << match.text << " score=" << match.score << '\n';
            if (match.text.find("NEXT") != std::string::npos) found = true;
        }
        if (!found) throw std::runtime_error("OCR_EXPECTED_TEXT_MISSING");
        }
        engine.cancel();
        }
        if (memory_cycles) {
            const auto maintenance = wvd::platform::optimize_idle_heap();
            if (!before.process_ok || !maintenance.before.process_ok || !maintenance.after.process_ok ||
                !maintenance.succeeded)
                throw std::runtime_error("OCR_MEMORY_MEASUREMENT_FAILED");
            std::cout << "memory_cycle=" << cycle + 1 << " before=" << before.private_bytes
                      << " released=" << maintenance.before.private_bytes
                      << " optimized=" << maintenance.after.private_bytes
                      << " heap_available=" << maintenance.heap_after.available
                      << " heap_complete=" << maintenance.heap_after.complete
                      << " heap_allocated=" << maintenance.heap_after.allocated
                      << " heap_committed=" << maintenance.heap_after.committed
                      << " optimize_us=" << maintenance.elapsed_us << '\n';
        }
        }
        if (lease.storage_stats().model_bytes != 0)
            throw std::runtime_error("OCR_LOADER_RETAINED_SOURCE_MODEL_BYTES");
        lease.verify_members();
        std::cout << "Frozen published model paths inferred successfully; source model buffers remain zero\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
