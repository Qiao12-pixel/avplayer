//
// Created by Joe on 26-3-23.
//

#ifndef BASEDEF_H
#define BASEDEF_H
enum AVFrameFlag {
    KKeyFrame = 1 << 0,//关键帧
    KFlush = 1 << 1,//刷新
    KEOS = 1 << 2,//结束
};
#endif //BASEDEF_H
