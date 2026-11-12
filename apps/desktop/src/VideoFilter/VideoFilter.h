//
// Created by Joe on 26-3-27.
//

#ifndef VIDEOFILTER_H
#define VIDEOFILTER_H

#include <string>
#include <unordered_map>

#include "../../include/IVideoFilter.h"

namespace av {
    class VideoFilter : public IVideoFilter {
    public:
        explicit VideoFilter(VideoFilterType type);
        ~VideoFilter() override = default;

        VideoFilterType GetType() const override;

        void SetFloat(const std::string& name, float value) override;
        float GetFloat(const std::string& name) override;

        void SetInt(const std::string& name, int value) override;
        int GetInt(const std::string& name) override;

        void SetString(const std::string& name, const std::string& value) override;
        std::string GetString(const std::string& name) override;

    private:
        VideoFilterType m_type;
        std::unordered_map<std::string, float> m_floatValues;
        std::unordered_map<std::string, int> m_intValues;
        std::unordered_map<std::string, std::string> m_stringValues;
    };
}

#endif //VIDEOFILTER_H
