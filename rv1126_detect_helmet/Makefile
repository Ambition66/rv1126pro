hide := @
ECHO := echo

CROSS_COMPILE ?= arm-linux-gnueabihf-
G++ ?= $(CROSS_COMPILE)g++
HOST_CXX ?= g++

TARGET := rv1126_detect_helmet_app

# ENABLE_RKNN=1 links the RV1126 RKNN runtime and enables real board inference.
# ENABLE_RKMEDIA=1 links RKMedia/FFmpeg and enables VI/VENC/RGA streaming.
# RKNN_INC uses the old RV1126 rknn_api.h, not the RKNPU2/RV1126B header.
ENABLE_RKNN ?= 1
ENABLE_RKMEDIA ?= 0
THIRD_PARTY_DIR ?= ./third_party/rv1126
RKNN_INC ?= $(THIRD_PARTY_DIR)/include/rknn
RKNN_LIB ?= $(THIRD_PARTY_DIR)/lib/platform/librknn_api.so
RV1126_PLATFORM_LIB ?= $(THIRD_PARTY_DIR)/lib/platform
RV1126_FFMPEG_INC ?= $(THIRD_PARTY_DIR)/include/ffmpeg
RV1126_FFMPEG_LIB ?= $(THIRD_PARTY_DIR)/lib/ffmpeg
RV1126_SRT_LIB ?= $(THIRD_PARTY_DIR)/lib/srt
RV1126_OPENSSL_LIB ?= $(THIRD_PARTY_DIR)/lib/openssl
RV1126_X264_LIB ?= $(THIRD_PARTY_DIR)/lib/x264

SRC := \
	src/main.cpp \
	src/ai/ai_frame_queue.cpp \
	src/ai/ai_result_manager.cpp \
	src/ai/ai_worker.cpp \
	src/ai/helmet_detector.cpp \
	src/ai/helmet_postprocess.cpp \
	src/ai/rknn_engine.cpp \
	src/media/media_pipeline.cpp

CFLAGS := -std=c++11 -Wall -Wextra -I./include
LD_FLAGS := -lpthread

ifeq ($(ENABLE_RKNN),1)
CFLAGS += -DENABLE_RKNN -I$(RKNN_INC)
LD_FLAGS += $(RKNN_LIB) -Wl,-rpath-link,$(RV1126_PLATFORM_LIB)
endif

ifeq ($(ENABLE_RKMEDIA),1)
CFLAGS += -DENABLE_RKMEDIA \
	-I$(THIRD_PARTY_DIR)/include/rkmedia \
	-I$(RV1126_FFMPEG_INC)
LD_FLAGS += \
	-L$(RV1126_FFMPEG_LIB) \
	-L$(RV1126_PLATFORM_LIB) \
	-L$(RV1126_SRT_LIB) \
	-L$(RV1126_OPENSSL_LIB) \
	-L$(RV1126_X264_LIB) \
	-leasymedia -ldrm -lrockchip_mpp -lrga \
	-lavformat -lavcodec -lavutil -lswresample \
	-lsrt -lssl -lcrypto -lx264 \
	-lasound -lv4l2 -lv4lconvert \
	-lpthread \
	-Wl,-rpath-link,$(RV1126_PLATFORM_LIB) \
	-Wl,-rpath-link,$(RV1126_FFMPEG_LIB) \
	-Wl,-rpath-link,$(RV1126_SRT_LIB) \
	-Wl,-rpath-link,$(RV1126_OPENSSL_LIB) \
	-Wl,-rpath-link,$(RV1126_X264_LIB)
endif

all:
	$(G++) $(SRC) $(CFLAGS) $(LD_FLAGS) -o $(TARGET)
	$(hide)$(ECHO) "Build Done: $(TARGET)"

stub:
	$(MAKE) ENABLE_RKNN=0 ENABLE_RKMEDIA=0

test-host:
	$(HOST_CXX) -std=c++11 -Wall -Wextra -I./include \
		tests/test_helmet_postprocess.cpp src/ai/helmet_postprocess.cpp \
		-o tests/test_helmet_postprocess
	./tests/test_helmet_postprocess

clean:
	rm -f $(TARGET) tests/test_helmet_postprocess
