/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "ring_buffer_lockfree.h"

CRingBufferLockFree::CRingBufferLockFree()
    : m_p_frame(NULL)
    , m_buffer_num(0)
    , m_buffer_size(0)
    , m_write_num(0)
    , m_render_read_num(-1)
    , m_encode_read_num(-1)
    , m_rending(false)
    , m_encoding(false)
    , m_dropped_frames(0)
{
}

CRingBufferLockFree::~CRingBufferLockFree()
{
    for (int i = 0; i < m_buffer_num; i++) {
        if (m_p_frame[i].p_buffer) {
            free(m_p_frame[i].p_buffer);
        }
    }
    if (m_p_frame) {
        free(m_p_frame);
    }
}

bool CRingBufferLockFree::set_property(int buffer_num, int buffer_size)
{
    m_p_frame = (st_frame_t*)malloc(buffer_num * sizeof(st_frame_t));
    if (NULL == m_p_frame) {
        return false;
    }
    
    m_buffer_size = buffer_size;
    for (int i = 0; i < buffer_num; i++) {
        m_p_frame[i].p_buffer = (unsigned char*)malloc(buffer_size);
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
    
    // Initialize atomic indices
    m_write_num.store(0, std::memory_order_relaxed);
    m_render_read_num.store(-1, std::memory_order_relaxed);
    m_encode_read_num.store(-1, std::memory_order_relaxed);
    
    return true;
}

st_frame_t* CRingBufferLockFree::get_buffer_to_fill()
{
    // Lock-free path: Use acquire-release semantics for producer
    long long current_write = m_write_num.load(std::memory_order_relaxed);
    long long current_render_read = m_render_read_num.load(std::memory_order_acquire);
    long long current_encode_read = m_encode_read_num.load(std::memory_order_acquire);
    
    if (m_buffer_num == 0) {
        return NULL;
    }
    
    // Back-pressure: Check if render consumer is too far behind
    // If queue is full, drop oldest frame to make room for new one
    if ((current_render_read >= 0) && ((current_write - current_render_read) >= m_buffer_num)) {
        // Queue is full - drop oldest frame by advancing render_read_num
        // Use compare-exchange to avoid race conditions
        long long expected = current_render_read;
        long long desired = expected + 1;
        while (!m_render_read_num.compare_exchange_weak(
            expected, 
            desired,
            std::memory_order_release,
            std::memory_order_relaxed))
        {
            current_render_read = expected;
            desired = current_render_read + 1;
            if ((current_write - current_render_read) < m_buffer_num) {
                // Another thread already freed up space
                break;
            }
        }
        
        // Update dropped frame count
        m_dropped_frames.fetch_add(1, std::memory_order_relaxed);
        
        // Return the buffer that was just freed up
        m_p_frame[current_render_read % m_buffer_num].frame_len = 0;
        return m_p_frame + (current_render_read % m_buffer_num);
    }
    
    // Check encode consumer back-pressure
    if ((current_encode_read >= 0) && ((current_write - current_encode_read) >= m_buffer_num)) {
        return NULL;  // Encode consumer is too far behind
    }
    
    // Return the next buffer for filling
    m_p_frame[current_write % m_buffer_num].frame_len = 0;
    return m_p_frame + (current_write % m_buffer_num);
}

void CRingBufferLockFree::buffer_filled()
{
    // Signal that the current buffer has been filled
    long long current_write = m_write_num.load(std::memory_order_relaxed);
    m_write_num.store(current_write + 1, std::memory_order_release);
}

st_frame_t* CRingBufferLockFree::get_frame_to_render()
{
    // Lock-free path for render consumer
    long long current_write = m_write_num.load(std::memory_order_acquire);
    long long current_render_read = m_render_read_num.load(std::memory_order_relaxed);
    
    if (current_write == 0) {
        return NULL;  // No frames available
    }
    
    // Handle initial state
    if (current_render_read < 0) {
        current_render_read = current_write - 1;
        m_render_read_num.store(current_render_read, std::memory_order_relaxed);
    }
    
    // Check if there are frames available
    if (current_write == current_render_read) {
        return NULL;  // No new frames
    }
    
    // Skip-ahead optimization: If render is falling behind, jump to newest frame
    // This prevents backlog from growing too large
    if ((current_write - current_render_read) > (m_buffer_num / 2)) {
        current_render_read = current_write - 1;
        m_render_read_num.store(current_render_read, std::memory_order_release);
    }
    
    // Advance render index for next call
    m_render_read_num.store(current_render_read + 1, std::memory_order_release);
    
    return m_p_frame + (current_render_read % m_buffer_num);
}

st_frame_t* CRingBufferLockFree::get_frame_to_encode()
{
    // Lock-free path for encode consumer (similar to render)
    long long current_write = m_write_num.load(std::memory_order_acquire);
    long long current_encode_read = m_encode_read_num.load(std::memory_order_relaxed);
    
    if (current_write == 0) {
        return NULL;
    }
    
    if (current_encode_read < 0) {
        current_encode_read = current_write - 1;
        m_encode_read_num.store(current_encode_read, std::memory_order_relaxed);
    }
    
    if (current_write == current_encode_read) {
        return NULL;
    }
    
    // Skip-ahead optimization
    if ((current_write - current_encode_read) > (m_buffer_num / 2)) {
        current_encode_read = current_write - 1;
        m_encode_read_num.store(current_encode_read, std::memory_order_release);
    }
    
    m_encode_read_num.store(current_encode_read + 1, std::memory_order_release);
    
    return m_p_frame + (current_encode_read % m_buffer_num);
}

void CRingBufferLockFree::stop_render()
{
    m_render_read_num.store(-1, std::memory_order_release);
}

void CRingBufferLockFree::stop_encode()
{
    m_encode_read_num.store(-1, std::memory_order_release);
}

st_frame_t* CRingBufferLockFree::get_buffer_by_index(int index)
{
    if (index >= m_buffer_num) {
        return NULL;
    }
    return m_p_frame + index;
}

int CRingBufferLockFree::GetQueueDepth()
{
    // Read with acquire semantics to ensure consistency
    long long current_write = m_write_num.load(std::memory_order_acquire);
    long long current_render_read = m_render_read_num.load(std::memory_order_acquire);
    
    if (current_write == 0 || current_render_read < 0) {
        return 0;
    }
    
    int depth = (int)(current_write - current_render_read);
    return depth > 0 ? depth : 0;
}

int CRingBufferLockFree::GetDroppedFrameCount()
{
    return m_dropped_frames.load(std::memory_order_relaxed);
}

void CRingBufferLockFree::ResetStatistics()
{
    m_dropped_frames.store(0, std::memory_order_relaxed);
}