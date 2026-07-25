#ifndef RV1126_DETECT_HELMET_TYPES_H
#define RV1126_DETECT_HELMET_TYPES_H

#include <stdint.h>

#define HELMET_MAX_DETECTIONS 32

// 检测类别。当前 helmet/no_helmet 是主业务类别，rider/electric_bike 为后续模型升级预留。
enum HelmetClassId {
    HELMET_CLASS_HELMET = 0,
    HELMET_CLASS_NO_HELMET = 1,
    HELMET_CLASS_RIDER = 2,
    HELMET_CLASS_ELECTRIC_BIKE = 3,
};

// AI 输入图像格式。媒体链路默认通过 RGA 输出 RGB888。
enum HelmetImageFormat {
    HELMET_IMAGE_UNKNOWN = 0,
    HELMET_IMAGE_NV12 = 1,
    HELMET_IMAGE_RGB888 = 2,
    HELMET_IMAGE_BGR888 = 3,
};

// 单个检测框，坐标使用当前输入帧坐标系。
typedef struct {
    int class_id;
    float confidence;
    int x;
    int y;
    int w;
    int h;
} helmet_detection_t;

// 单帧检测结果，后续叠框、报警、截图、上报都消费这个结构。
typedef struct {
    int frame_id;
    uint64_t timestamp_ms;
    int width;
    int height;
    int detection_count;
    helmet_detection_t detections[HELMET_MAX_DETECTIONS];
} helmet_result_t;

// AI 输入帧。AiFrameQueue 会复制到内部复用缓冲池，消费者处理后调用 ReleaseFrame。
typedef struct {
    int frame_id;
    uint64_t timestamp_ms;
    int width;
    int height;
    HelmetImageFormat format;
    unsigned char *data;
    int size;
} helmet_frame_t;

#endif
