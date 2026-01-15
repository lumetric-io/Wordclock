#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

// Mock Arduino String class for native testing
class String {
public:
    String() : data_("") {}
    String(const char* cstr) : data_(cstr ? cstr : "") {}
    String(const std::string& str) : data_(str) {}
    String(int num) : data_(std::to_string(num)) {}
    String(unsigned int num) : data_(std::to_string(num)) {}
    String(long num) : data_(std::to_string(num)) {}
    String(unsigned long num) : data_(std::to_string(num)) {}
    String(float num, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, num);
        data_ = buf;
    }
    
    const char* c_str() const { return data_.c_str(); }
    size_t length() const { return data_.length(); }
    
    String& operator=(const String& other) {
        data_ = other.data_;
        return *this;
    }
    
    String& operator=(const char* cstr) {
        data_ = cstr ? cstr : "";
        return *this;
    }
    
    String operator+(const String& other) const {
        return String((data_ + other.data_).c_str());
    }
    
    String operator+(const char* cstr) const {
        return String((data_ + (cstr ? cstr : "")).c_str());
    }
    
    String operator+(int num) const {
        return String((data_ + std::to_string(num)).c_str());
    }
    
    bool operator==(const String& other) const {
        return data_ == other.data_;
    }
    
    bool operator==(const char* cstr) const {
        return data_ == (cstr ? cstr : "");
    }
    
    bool operator!=(const String& other) const {
        return data_ != other.data_;
    }
    
    bool operator!=(const char* cstr) const {
        return data_ != (cstr ? cstr : "");
    }
    
    void toLowerCase() {
        for (char& c : data_) {
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        }
    }
    
    void toUpperCase() {
        for (char& c : data_) {
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }
    }
    
    void trim() {
        size_t start = data_.find_first_not_of(" \t\n\r");
        size_t end = data_.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) {
            data_ = "";
        } else {
            data_ = data_.substr(start, end - start + 1);
        }
    }
    
    int indexOf(char ch) const {
        size_t pos = data_.find(ch);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    
    String substring(size_t start) const {
        if (start >= data_.length()) return String("");
        return String(data_.substr(start).c_str());
    }
    
    String substring(size_t start, size_t end) const {
        if (start >= data_.length()) return String("");
        if (end > data_.length()) end = data_.length();
        if (end <= start) return String("");
        return String(data_.substr(start, end - start).c_str());
    }
    
    int toInt() const {
        return std::atoi(data_.c_str());
    }
    
    // ArduinoJson compatibility methods
    size_t write(uint8_t c) {
        data_ += static_cast<char>(c);
        return 1;
    }
    
    size_t write(const uint8_t* buffer, size_t size) {
        data_.append(reinterpret_cast<const char*>(buffer), size);
        return size;
    }
    
    // Additional overload for ArduinoJson Writer template
    size_t write(const char* buffer, size_t size) {
        if (buffer && size > 0) {
            data_.append(buffer, size);
        }
        return size;
    }
    
    // Conversion to std::string for ArduinoJson
    operator std::string() const {
        return data_;
    }
    
    // Get underlying std::string
    const std::string& str() const {
        return data_;
    }
    
private:
    std::string data_;
};

// Mock millis
static unsigned long mockMillis = 0;
inline unsigned long millis() { return mockMillis; }
inline void setMockMillis(unsigned long value) { mockMillis = value; }

// Mock delay (no-op in tests)
inline void delay(unsigned long ms) { (void)ms; }

#endif // MOCK_ARDUINO_H

