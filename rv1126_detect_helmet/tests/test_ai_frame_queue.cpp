#include <assert.h>
#include <string.h>

#include "ai_frame_queue.h"

static helmet_frame_t make_frame(int id, unsigned char *data, int size) {
    helmet_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.frame_id = id;
    frame.width = size;
    frame.height = 1;
    frame.format = HELMET_IMAGE_RGB888;
    frame.data = data;
    frame.size = size;
    return frame;
}

int main() {
    AiFrameQueue queue;
    unsigned char first[] = {1, 1, 1};
    unsigned char latest[] = {2, 2, 2};

    helmet_frame_t frame = make_frame(1, first, sizeof(first));
    assert(queue.PushLatest(frame) == 0);
    frame = make_frame(2, latest, sizeof(latest));
    assert(queue.PushLatest(frame) == 0);
    assert(queue.Size() == 1);

    helmet_frame_t output;
    assert(queue.PopLatest(&output) == 0);
    assert(output.frame_id == 2);
    assert(output.size == (int)sizeof(latest));
    assert(memcmp(output.data, latest, sizeof(latest)) == 0);
    unsigned char *pooled_address = output.data;
    assert(queue.ReleaseFrame(&output) == 0);
    assert(output.data == NULL);

    unsigned char third[] = {3, 3, 3};
    frame = make_frame(3, third, sizeof(third));
    assert(queue.PushLatest(frame) == 0);
    assert(queue.PopLatest(&output) == 0);
    assert(output.data == pooled_address);
    assert(memcmp(output.data, third, sizeof(third)) == 0);
    assert(queue.ReleaseFrame(&output) == 0);

    queue.Close();
    assert(queue.PopLatest(&output) == 1);
    return 0;
}
