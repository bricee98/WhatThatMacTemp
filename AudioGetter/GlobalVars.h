// GlobalVars.h
#ifndef GlobalVars_h
#define GlobalVars_h

#include <vector>
#include <mutex>
#include <CoreAudio/CoreAudio.h>
#include <iostream> // To allow logging via std::cout


class RingBuffer {
public:
    RingBuffer(size_t size) : buffer(size, 0), writePos(0), readPos(0) {}

    void write(const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        const float* dataFloat = static_cast<const float*>(data);
        size_t floatSize = size / sizeof(float);
        for (size_t i = 0; i < floatSize; ++i) {
            buffer[writePos] = dataFloat[i];
            writePos = (writePos + 1) % buffer.size();
        }
        std::cout << "Write function called. " << floatSize << " float samples written. Write position is now: " << writePos << std::endl;
    }


    void read(void* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        float* dataFloat = static_cast<float*>(data);
        size_t floatSize = size / sizeof(float);
        for (size_t i = 0; i < floatSize; ++i) {
            dataFloat[i] = buffer[readPos];
            readPos = (readPos + 1) % buffer.size();
        }
    }
    
    void verifyBuffer() {
        std::lock_guard<std::mutex> lock(mutex);
        
        std::cout << "Verifying buffer contents...\n";
        
        size_t num_samples_to_print = 10;  // change as needed
        for(size_t i = 0; i < num_samples_to_print; ++i) {
            std::cout << "Sample [" << i << "] = " << buffer[i] << "\n";
        }

        std::cout << "...\n";
        
        for(size_t i = buffer.size() - num_samples_to_print; i < buffer.size(); ++i) {
            std::cout << "Sample [" << i << "] = " << buffer[i] << "\n";
        }
    }


private:
    std::vector<float> buffer;
    size_t writePos;
    size_t readPos;
    std::mutex mutex;
};



extern RingBuffer globalRingBuffer;
extern AudioStreamBasicDescription globalStreamFormat;

#endif /* GlobalVars_h */
