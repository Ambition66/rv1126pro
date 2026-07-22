hide := @
ECHO := echo

G++ ?= /opt/rv1126_rv1109_linux_sdk_v1.8.0_20210224/prebuilts/gcc/linux-x86/arm/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/bin/arm-linux-gnueabihf-g++

TARGET := rv1126_detect_helmet_app

# ENABLE_RKNN=1 links the RV1126 RKNN runtime and enables real board inference.
# ENABLE_RKMEDIA=1 links RKMedia/FFmpeg and enables VI/VENC/RGA streaming.
# RKNN_INC uses the old RV1126 rknn_api.h, not the RKNPU2/RV1126B header.
ENABLE_RKNN ?= 1
ENABLE_RKMEDIA ?= 0
RKNN_INC ?= ../rv1126_AI/face_smoking_phone_sleep/include
RKNN_LIB ?= ../rv1126_ffmpeg/rv1126_lib/librknn_api.so
RV1126_LIB_DIR ?= ../rv1126_ffmpeg/rv1126_lib
RV1126_FFMPEG_DIR ?= ../rv1126_ffmpeg

SRC := \
	src/main.cpp \
	src/ai/ai_frame_queue.cpp \
	src/ai/ai_result_manager.cpp \
	src/ai/ai_worker.cpp \
	src/ai/helmet_detector.cpp \
	src/ai/rknn_engine.cpp \
	src/media/media_pipeline.cpp

CFLAGS := -std=c++11 -Wall -Wextra -I./include
LD_FLAGS := -lpthread

ifeq ($(ENABLE_RKNN),1)
CFLAGS += -DENABLE_RKNN -I$(RKNN_INC)
LD_FLAGS += $(RKNN_LIB) -Wl,-rpath-link,$(RV1126_LIB_DIR)
endif

ifeq ($(ENABLE_RKMEDIA),1)
CFLAGS += -DENABLE_RKMEDIA \
	-I$(RV1126_FFMPEG_DIR)/include/rkmedia \
	-I$(RV1126_FFMPEG_DIR)/arm32_ffmpeg_srt/include
LD_FLAGS += \
	-L$(RV1126_FFMPEG_DIR)/rv1126_lib \
	-L$(RV1126_FFMPEG_DIR)/arm32_ffmpeg_srt/lib \
	-L$(RV1126_FFMPEG_DIR)/opt/arm_libsrt/lib \
	-L$(RV1126_FFMPEG_DIR)/opt/arm_openssl/lib \
	-leasymedia -ldrm -lrockchip_mpp -lrga \
	-lavformat -lavcodec -lavutil -lswresample \
	-lsrt -lssl -lcrypto \
	-lasound -lv4l2 -lv4lconvert \
	-lpthread \
	-Wl,-rpath-link,$(RV1126_FFMPEG_DIR)/rv1126_lib \
	-Wl,-rpath-link,$(RV1126_FFMPEG_DIR)/arm32_ffmpeg_srt/lib \
	-Wl,-rpath-link,$(RV1126_FFMPEG_DIR)/opt/arm_libsrt/lib \
	-Wl,-rpath-link,$(RV1126_FFMPEG_DIR)/opt/arm_openssl/lib
endif

all:
	$(G++) $(SRC) $(CFLAGS) $(LD_FLAGS) -o $(TARGET)
	$(hide)$(ECHO) "Build Done: $(TARGET)"

stub:
	$(MAKE) ENABLE_RKNN=0 ENABLE_RKMEDIA=0

clean:
	rm -f $(TARGET)
