/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <chrono>
#include "command_ring.hpp"
#include "rdp_device.hpp"
#include "thread_id.hpp"
#include <assert.h>

namespace RDP
{
void CommandRing::init(
#ifdef PARALLEL_RDP_SHADER_DIR
		Granite::Global::GlobalManagersHandle global_handles_,
#endif
		CommandProcessor *processor_, unsigned count)
{
	assert((count & (count - 1)) == 0);
	teardown_thread();
	processor = processor_;
	ring.resize(count);
	write_count = 0;
	completed_count = 0;
	read_count = 0;
#ifdef PARALLEL_RDP_SHADER_DIR
	global_handles = std::move(global_handles_);
#endif
	thr = std::thread(&CommandRing::thread_loop, this);
}

void CommandRing::teardown_thread()
{
	if (thr.joinable())
	{
		enqueue_command(0, nullptr);
		thr.join();
	}
}

CommandRing::~CommandRing()
{
	teardown_thread();
}

void CommandRing::wake()
{
	{
		std::lock_guard<std::mutex> holder{lock};
	}
	cond.notify_one();
}

void CommandRing::drain()
{
	while (completed_count.load(std::memory_order_acquire) != write_count.load(std::memory_order_relaxed))
	{
		if (sleeping.load())
			wake();
		std::this_thread::yield();
	}
}

void CommandRing::enqueue_command(unsigned num_words, const uint32_t *words)
{
	uint64_t w = write_count.load(std::memory_order_relaxed);
	while (w + num_words + 1 > read_count.load(std::memory_order_acquire) + ring.size())
	{
		// Full: the consumer is behind.
		if (sleeping.load())
			wake();
		std::this_thread::yield();
	}

	size_t mask = ring.size() - 1;
	ring[w++ & mask] = num_words;
	for (unsigned i = 0; i < num_words; i++)
		ring[w++ & mask] = words[i];
	write_count.store(w);

	if (sleeping.load())
		wake();
}

void CommandRing::thread_loop()
{
	Util::register_thread_index(0);

#ifdef PARALLEL_RDP_SHADER_DIR
	// Here to let the RDP play nice with full Granite.
	// When we move to standalone Granite, we won't need to interact with global subsystems like this.
	Granite::Global::set_thread_context(*global_handles);
	global_handles.reset();
#endif

	std::vector<uint32_t> tmp_buffer;
	tmp_buffer.reserve(64);
	size_t mask = ring.size() - 1;

	for (;;)
	{
		uint64_t r = read_count.load(std::memory_order_relaxed);
		if (r == write_count.load(std::memory_order_acquire))
		{
			// Nothing yet: look again for a short while (commands come in
			// bursts), then sleep until the producer wakes us.
			bool got = false;
			for (unsigned spin = 0; spin < 2000 && !got; spin++)
			{
#if defined(__x86_64__) || defined(_M_X64)
				__builtin_ia32_pause();
#endif
				got = r != write_count.load(std::memory_order_acquire);
			}
			if (!got)
			{
				sleeping.store(true);
				{
					std::unique_lock<std::mutex> holder{lock};
					got = cond.wait_for(holder, std::chrono::microseconds(500),
					                    [this, r]() { return r != write_count.load(); });
				}
				sleeping.store(false);
			}
			if (!got)
			{
				// If we don't receive commands at a steady pace,
				// notify rendering thread that we should probably kick some work.
				uint32_t idle = uint32_t(Op::MetaIdle) << 24;
				processor->enqueue_command_direct(1, &idle);
				continue;
			}
		}

		uint32_t num_words = ring[r++ & mask];
		tmp_buffer.resize(num_words);
		for (uint32_t i = 0; i < num_words; i++)
			tmp_buffer[i] = ring[r++ & mask];
		read_count.store(r, std::memory_order_release);

		if (tmp_buffer.empty())
			break;

		processor->enqueue_command_direct(tmp_buffer.size(), tmp_buffer.data());
		completed_count.store(r, std::memory_order_release);
	}
}
}
