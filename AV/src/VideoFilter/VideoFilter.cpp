//
// Created by Joe on 26-3-27.
//

#include "VideoFilter.h"

namespace av {
    VideoFilter::VideoFilter(VideoFilterType type) : m_type(type) {}

    VideoFilterType VideoFilter::GetType() const {
        return m_type;
    }

    void VideoFilter::SetFloat(const std::string& name, float value) {
        m_floatValues[name] = value;
    }

    float VideoFilter::GetFloat(const std::string& name) {
        auto it = m_floatValues.find(name);
        return it != m_floatValues.end() ? it->second : 0.0f;
    }

    void VideoFilter::SetInt(const std::string& name, int value) {
        m_intValues[name] = value;
    }

    int VideoFilter::GetInt(const std::string& name) {
        auto it = m_intValues.find(name);
        return it != m_intValues.end() ? it->second : 0;
    }

    void VideoFilter::SetString(const std::string& name, const std::string& value) {
        m_stringValues[name] = value;
    }

    std::string VideoFilter::GetString(const std::string& name) {
        auto it = m_stringValues.find(name);
        return it != m_stringValues.end() ? it->second : std::string();
    }
}
