#include <pch.h>

#include "ring_buffer.h"
#include <stdlib.h>
CRingBuffer::CRingBuffer()
{
    m_p_frame = NULL;
    m_buffer_num = 0;
    m_buffer_size = 0;
    m_write_num = 0;
    m_render_read_num = -1;
    m_encode_read_num = -1;
    m_rending = false;
    m_encoding = false;
    m_writing = false;
}

CRingBuffer::~CRingBuffer()
{
    for (int i = 0; i < m_buffer_num; i++) {
        if (m_p_frame[i].p_buffer) {
            free(m_p_frame[i].p_buffer);
        }
        else {
            break;
        }
    }
    if (m_p_frame) {
        free(m_p_frame);
    }
}

bool CRingBuffer::set_property(int buffer_num, int buffer_size)
{
    m_p_frame = (st_frame_t *)malloc(buffer_num*sizeof(st_frame_t));
    if (NULL == m_p_frame) {
        return false;
    }
    m_buffer_size = buffer_size;
    for (int i = 0; i < buffer_num; i++) {
        m_p_frame[i].p_buffer = (unsigned char *)malloc(buffer_size);
        if (NULL == m_p_frame[i].p_buffer) {
            m_buffer_num = i;
            return false;
        }
        m_p_frame[i].buffer_len = buffer_size;
        m_p_frame[i].frame_len = 0;
        m_p_frame[i].user_point = NULL;
        m_p_frame[i].ts = 0;
    }
    m_buffer_num = buffer_num;
    return true;
}

st_frame_t *CRingBuffer::get_buffer_to_fill()
{
    if (m_writing) {
        return m_p_frame + (m_write_num % m_buffer_num);
    }
    if (m_buffer_num == 0) {
        return NULL;
    }
    if ((m_render_read_num >= 0) && ((m_write_num - m_render_read_num) >= m_buffer_num)) {
        return NULL;
    }
    if ((m_encode_read_num >= 0) && ((m_write_num - m_encode_read_num) >= m_buffer_num)) {
        return NULL;
    }
    m_writing = true;
    m_p_frame[m_write_num % m_buffer_num].frame_len = 0;
    return m_p_frame + (m_write_num % m_buffer_num);
}

void CRingBuffer::buffer_filled()
{
    if (m_writing) {
        m_write_num++;
        m_writing = false;
    }
}

st_frame_t * CRingBuffer::get_frame_to_render()
{
    if (m_write_num == 0) {
        return NULL;
    }
    if (m_rending) {
        m_render_read_num++;
        m_rending = false;
    }
    else if (m_render_read_num < 0) {
        m_render_read_num = (m_write_num ? m_write_num:1) - 1;
    }
    if (m_write_num == m_render_read_num) {
        return NULL;
    }
    if ((m_write_num - m_render_read_num) > (m_buffer_num / 2)) {
        m_render_read_num = m_write_num - 1;
    }
    m_rending = true;
    return m_p_frame + (m_render_read_num % m_buffer_num); 
}

st_frame_t * CRingBuffer::get_frame_to_encode()
{
    if (m_write_num == 0) {
        return NULL;
    }
    if (m_encoding) {
        m_encode_read_num++;
        m_encoding = false;
    }
    else if (m_encode_read_num < 0) {
        m_encode_read_num = (m_write_num ? m_write_num : 1) - 1;
    }
    if (m_write_num == m_encode_read_num) {
        return NULL;
    }
    if ((m_write_num - m_encode_read_num) > (m_buffer_num / 2)) {
        m_encode_read_num = m_write_num - 1;
    }
    m_encoding = true;
    return m_p_frame + (m_encode_read_num % m_buffer_num);
}
void CRingBuffer::stop_render()
{
    m_render_read_num = -1;
}
void CRingBuffer::stop_encode()
{
    m_encode_read_num = -1;
}
st_frame_t *CRingBuffer::get_buffer_by_index(int index)
{
    if (index >= m_buffer_num) {
        return NULL;
    }
    return m_p_frame + index;
}
