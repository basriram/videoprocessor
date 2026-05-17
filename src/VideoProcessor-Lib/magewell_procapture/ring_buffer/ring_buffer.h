#pragma once
#include <stdio.h>
#include <atomic>

typedef struct st_frame {
    unsigned char*p_buffer;
    long long ts;
    unsigned int buffer_len;
    unsigned int frame_len;
    void *user_point;
}st_frame_t;

class CRingBuffer {
public:
    CRingBuffer();
    ~CRingBuffer();
    bool set_property(int buffer_num, int buffer_size);
    st_frame_t* get_buffer_to_fill();
    void buffer_filled();
    
    st_frame_t *get_frame_to_render();
    st_frame_t *get_frame_to_encode();

    void stop_render();
    void stop_encode();
    st_frame_t *get_buffer_by_index(int index);
    
    // Back-pressure and statistics
    int GetQueueDepth();
    int GetDroppedFrameCount();
    void ResetStatistics();
    
private:
    st_frame_t *m_p_frame;
    int        m_buffer_num;
    int        m_buffer_size;
    bool       m_writing;
    long long  m_write_num;
    bool       m_rending;
    long long  m_render_read_num;
    bool       m_encoding;
    long long  m_encode_read_num;
    
    // Statistics for back-pressure monitoring
    std::atomic<int> m_dropped_frames;
};
