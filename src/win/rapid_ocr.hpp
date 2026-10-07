// rapid_ocr.hpp — RapidOCR line recognition (PaddleOCR models, ONNX Runtime).
// Open source: RapidOCR / PaddleOCR models Apache-2.0, ONNX Runtime MIT. Runs
// on the CPU of this PC; nothing goes anywhere.
//
// onnxruntime.dll is delay-loaded: RuntimeAvailable() loads it from the exe's
// folder first; without it, RapidOCR is simply not offered.
#pragma once

#include <memory>
#include <string>

#include "core/image.hpp"
#include "core/rapid_rec.hpp"

namespace gct {

class RapidRecognizer {
public:
    RapidRecognizer();
    ~RapidRecognizer();
    RapidRecognizer(const RapidRecognizer&) = delete;
    RapidRecognizer& operator=(const RapidRecognizer&) = delete;

    // onnxruntime.dll is next to the exe and loads (built with RapidOCR support).
    static bool RuntimeAvailable(std::wstring* error);

    // A recognition model (.onnx) and its dictionary (.txt).
    bool Load(const std::wstring& modelPath, const std::wstring& dictPath, std::wstring* error);
    bool Ready() const;

    // One text line (a crop of the picture).
    RecResult Recognize(const Image& line, bool invert, std::wstring* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct RapidModelGroup;
// Model and dictionary of a group are in `dir`.
bool RapidGroupInstalled(const RapidModelGroup& g, const std::wstring& dir);
// Where a group is: `userDir` (downloaded) or the `rapid` folder next to the program (shipped with it); empty if
// neither has it.
std::wstring RapidGroupDir(const RapidModelGroup& g, const std::wstring& userDir);
// Downloads a group into `dir` (official RapidOCR files, SHA-256 checked). Blocking: call from a worker thread.
bool DownloadRapidGroup(const RapidModelGroup& g, const std::wstring& dir, std::wstring* error);

}  // namespace gct
