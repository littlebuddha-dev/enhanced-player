// ./AudioDecoderFactory.cpp
#include "AudioDecoderFactory.h"
#include "SndfileDecoder.h"
#include "MPG123Decoder.h" // MPG123Decoder を利用
#include <iostream>

std::unique_ptr<AudioDecoder> AudioDecoderFactory::createDecoder(const std::string& filePath) {
    std::string extension = filePath.substr(filePath.find_last_of("."));
    for (char& c : extension) {
        c = tolower(c);
    }

    std::unique_ptr<AudioDecoder> decoder = nullptr;

    if (extension == ".mp3") {
        std::cout << "Info: MP3 file detected. Using MPG123 Decoder." << std::endl;
        decoder = std::make_unique<MPG123Decoder>();
    } else {
        std::cout << "Info: " << extension << " file detected. Using Sndfile Decoder." << std::endl;
        decoder = std::make_unique<SndfileDecoder>();
    }

    if (decoder && decoder->open(filePath)) {
        return decoder;
    } else {
        std::cerr << "Factory Error: Failed to open file with the selected decoder." << std::endl;
        return nullptr;
    }
}