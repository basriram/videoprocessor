/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

// st_frame_t is defined in ring_buffer.h
#include "ring_buffer.h"

#ifndef RING_BUFFER_LOCKFREE_H
#define RING_BUFFER_LOCKFREE_H

#include <stdio.h>
#include <atomic>
#include <cstdlib>

/**
 * Lock-Free Ring Buffer for 4K HDR60 Video Capture
 * 
 * This implementation uses atomic operations instead of mutexes to eliminate
 * contention in the critical frame capture path. It implements a single-producer,
 * single-consumer (SPSC) queue design optimized for low-latency video processing.
 * 
 * Key features:
 * - No mutex locks in the hot path
 * - Memory ordering optimized for x86/x64 architectures
 * - Back-pressure support to prevent buffer overflow
 * - Statistics tracking for dropped frames
 */
class CRingBufferLockFree
{
public:
    CRingBufferLockFree();
    ~CRingBufferLockFree();

    /**
     * Initialize the ring buffer
     * @param buffer_num Number of buffers in the ring
     * @param buffer_size Size of each buffer in bytes
     * @return true on success
     */
    bool set_property(int buffer_num, int buffer_size);

    /**
     * Get a buffer for the capture thread to fill
     * @return Pointer to buffer, or NULL if no buffer available
     */
    st_frame_t* get_buffer_to_fill();

    /**
     * Signal that the buffer has been filled by the capture hardware
     */
    void buffer_filled();

    /**
     * Get a frame for the render thread to process
     * @return Pointer to frame, or NULL if no frame available
     */
    st_frame_t* get_frame_to_render();

    /**
     * Get a frame for the encode thread to process
     * @return Pointer to frame, or NULL if no frame available
     */
    st_frame_t* get_frame_to_encode();

    /**
     * Stop the render consumer
     */
    void stop_render();

    /**
     * Stop the encode consumer
     */
    void stop_encode();

    /**
     * Get buffer by index (for initial pinning)
     * @param index Buffer index
     * @return Pointer to buffer, or NULL if index out of range
     */
    st_frame_t* get_buffer_by_index(int index);

    /**
     * Get current queue depth (number of frames waiting to be rendered)
     * @return Queue depth
     */
    int GetQueueDepth();

    /**
     * Get number of dropped frames due to back-pressure
     * @return Dropped frame count
     */
    int GetDroppedFrameCount();

    /**
     * Reset statistics
     */
    void ResetStatistics();

private:
    st_frame_t* m_p_frame;
    int         m_buffer_num;
    int         m_buffer_size;
    
    // Atomic indices for lock-free operation
    // Use cache-line alignment to prevent false sharing
    std::atomic<long long> m_write_num;        // Producer write index
    char pad1[64 - sizeof(std::atomic<long long>)];  // Padding for cache line
    
    std::atomic<long long> m_render_read_num;  // Render consumer read index
    char pad2[64 - sizeof(std::atomic<long long>)];  // Padding for cache line
    
    std::atomic<long long> m_encode_read_num;  // Encode consumer read index
    char pad3[64 - sizeof(std::atomic<long long>)];  // Padding for cache line
    
    // Consumer state flags
    std::atomic<bool> m_rending;   // Render thread is processing
    std::atomic<bool> m_encoding;  // Encode thread is processing
    
    // Statistics (relaxed ordering is sufficient)
    std::atomic<int> m_dropped_frames;
};

#endif // RING_BUFFER_LOCKFREE_H
