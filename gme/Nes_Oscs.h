// Private oscillators used by Nes_Apu

// Nes_Snd_Emu 0.1.8
#ifndef NES_OSCS_H
#define NES_OSCS_H

#include "blargg_common.h"
#include "Blip_Buffer.h"
#include "Music_Emu.h"
#include <stdio.h>
#include <math.h>

class Nes_Apu;

struct Nes_Osc
{
	int index;
	unsigned char regs [4];
	bool reg_written [4];
	Blip_Buffer* output;
	int length_counter;// length counter (0 if unused by oscillator)
	int delay;      // delay until next (potential) transition
	int last_amp;   // last amplitude oscillator was outputting

	// MIDI state:
	nes_time_t abs_time;
	double clock_rate_;
	unsigned char period_midi[0x800];
	short period_cents[0x800];

	virtual unsigned char midi_note_a() const = 0;
	virtual bool midi_pitch_wheel_enabled() const { return midi_channel() != 9; }

	virtual void set_clock_rate(double clock_rate) {
		clock_rate_ = clock_rate;
		// Pre-compute period->MIDI calculation:
		// Concert A# = 116.540940
		//     NES A# = 116.521662
		// Concert A  = 110
		//     NES A  = 109.981803632778603
		for (int p = 0; p < 0x800; ++p) {
			double f = clock_rate_ / (16 * (p + 1));
			double n = (log(f / 54.99090178) / log(2)) * 12;
			int m = round(n) + midi_note_a();
			period_midi[p] = m;
			period_cents[p] = (short)((n - round(n)) * 0xFFF) + 0x2000;
		}
	}

	virtual unsigned char midi_channel() const { return index; }
	virtual unsigned char midi_note() const { return period_midi[period()]; }
	virtual unsigned char midi_note_volume() const = 0;
	virtual unsigned char midi_channel_volume() const = 0;

	MidiTrack midi;
	int last_tick;
	unsigned char last_midi_note;
	unsigned char last_midi_channel;
	unsigned char last_midi_channel_volume[16];
	int note_on_period;
	short last_wheel;
	short last_wheel_emit[16];

	double seconds(nes_time_t time) {
		return (double)(abs_time + time) / clock_rate_;
	}

	int abs_tick(nes_time_t time) {
		double s = seconds(time);
		// NOTE: I dont know why this 47.8946 multiplier is necessary. REAPER doesnt load the file right without it.
		// I suspect this is a bug in REAPER and this multiplier is just junk that happens to convert the fps:ticks
		// format into a more traditional tempo divisor (where REAPER is assuming bit 15 is off of the divisor
		// specifier).
		int tick = (int)(s * 47.89463181001796 * Music_Emu::frames_per_second * Music_Emu::ticks_per_frame);
		return tick;
	}

	void midi_write_note_on(nes_time_t time) {
		midi.write_3(
			abs_tick(time),
			(0x90 | (midi_channel() & 0x0F)),
			midi_note(),
			midi_note_volume()
		);
	}

	void midi_write_note_off(nes_time_t time) {
		midi.write_3(
			abs_tick(time),
			(0x80 | (last_midi_channel & 0x0F)),
			last_midi_note,
			0
		);
	}

	void midi_write_channel_volume(nes_time_t time, unsigned char channel) {
		midi.write_3(
			abs_tick(time),
			0xB0 | (channel & 0x0F),
			0x07,	// channel volume
			midi_channel_volume()
		);
	}

	void midi_write_pitch_wheel(nes_time_t time, unsigned char channel, short wheel) {
		midi.write_3(
			abs_tick(time),
			0xE0 | channel,
			wheel & 0x7F,
			(wheel >> 7) & 0x7F
		);
		last_wheel_emit[channel] = wheel;
	}

	const int wheel_threshold = 384;

	virtual void note_on(nes_time_t time) {
		unsigned char m = midi_note();
		unsigned char new_midi_channel = midi_channel();

		if (m != last_midi_note) {
			note_off(time);
			if (midi_channel_volume() != last_midi_channel_volume[new_midi_channel]) {
				midi_write_channel_volume(time, new_midi_channel);
				last_midi_channel_volume[new_midi_channel] = midi_channel_volume();
			}
			note_on_period = period();
			if (midi_pitch_wheel_enabled()) {
				if (abs(period_cents[note_on_period]-0x2000) < wheel_threshold &&
					last_wheel_emit[new_midi_channel] != 0x2000)
				{
					// Reset pitch wheel to 0:
					midi_write_pitch_wheel(time, new_midi_channel, 0x2000);
				}
			}
			midi_write_note_on(time);
			last_midi_channel = new_midi_channel;
		} else {
			if (midi_channel_volume() != last_midi_channel_volume[last_midi_channel]) {
				// Update last channel played on's volume since we don't really support switching
				// duty cycle without restarting the note (i.e. playing it across multiple channels).
				midi_write_channel_volume(time, last_midi_channel);
				last_midi_channel_volume[last_midi_channel] = midi_channel_volume();
			}
		}

		if (midi_pitch_wheel_enabled()) {
			// Period is changing too finely for MIDI note to change:
			int wheel = period_cents[period()];
			if (abs(wheel-0x2000) >= wheel_threshold ||
				abs(period_cents[note_on_period]-wheel) >= wheel_threshold)
			{
				if (last_wheel_emit[last_midi_channel] != wheel) {
					// Emit pitch wheel change:
					midi_write_pitch_wheel(time, last_midi_channel, wheel);
				}
			}
		}

		// last_wheel = wheel;
		last_midi_note = m;
	}
	virtual void note_off(nes_time_t time) {
		if (last_midi_note == 0) {
			return;
		}

		midi_write_note_off(time);

		last_midi_note = 0;
	}

	void clock_length( int halt_mask );
	virtual int period() const {
		return (regs [3] & 7) * 0x100 + (regs [2] & 0xFF);
	}
	void reset() {
		delay = 0;
		last_amp = 0;
		last_midi_note = 0;
		abs_time = 0;
		for (int i = 0; i < 16; i++) {
			last_midi_channel_volume[i] = 0;
			last_wheel_emit[i] = 0x2000;
		}

		midi.mtrk.resize(30000);
		midi.length = 0;
	}
	int update_amp( int amp ) {
		int delta = amp - last_amp;
		last_amp = amp;
		return delta;
	}
};

struct Nes_Envelope : Nes_Osc
{
	int envelope;
	int env_delay;
	
	void clock_envelope();
	int volume() const;
	void reset() {
		envelope = 0;
		env_delay = 0;
		Nes_Osc::reset();
	}

	unsigned char midi_note_volume() const { return 112; }
	unsigned char midi_channel_volume() const { return (unsigned char)(volume() * 8); }
};

// Nes_Square
struct Nes_Square : Nes_Envelope
{
	enum { negate_flag = 0x08 };
	enum { shift_mask = 0x07 };
	enum { phase_range = 8 };
	int phase;
	int sweep_delay;
	
	typedef Blip_Synth<blip_good_quality,1> Synth;
	Synth const& synth; // shared between squares
	
	Nes_Square( Synth const* s ) : synth( *s ) { }

	// 45 = MIDI A3 (110 Hz)
	unsigned char midi_note_a() const { return 33; }
	unsigned char midi_channel() const { return (index * 4) + duty_select(); }

	int duty_select() const { return (regs [0] >> 6) & 3; }
	void clock_sweep( int adjust );
	void run( nes_time_t, nes_time_t );
	void reset() {
		sweep_delay = 0;
		Nes_Envelope::reset();
	}
	nes_time_t maintain_phase( nes_time_t time, nes_time_t end_time,
			nes_time_t timer_period );
};

// Nes_Triangle
struct Nes_Triangle : Nes_Osc
{
	enum { phase_range = 16 };
	int phase;
	int linear_counter;
	Blip_Synth<blip_med_quality,1> synth;
	
	int calc_amp() const;
	unsigned char midi_note_volume() const { return 120; }
	unsigned char midi_channel_volume() const { return 120; }
	// 33 = MIDI A2 (110 Hz)
	unsigned char midi_note_a() const { return 21; }
	unsigned char midi_channel() const { return 8; }

	void run( nes_time_t, nes_time_t );
	void clock_linear_counter();
	void reset() {
		linear_counter = 0;
		phase = 1;
		Nes_Osc::reset();
	}
	nes_time_t maintain_phase( nes_time_t time, nes_time_t end_time,
			nes_time_t timer_period );
};

struct Noise_Remapping {
	int src_period;
	int dest_midi_note;
};

// Nes_Noise
struct Nes_Noise : Nes_Envelope
{
	int noise;
	Blip_Synth<blip_med_quality,1> synth;

	// Support for remapping noise period to specific MIDI note:
	mutable blargg_vector<Noise_Remapping> remappings;
	Noise_Remapping *find_remapping() const {
		Noise_Remapping *r = remappings.begin();
		if (r != 0) {
			for (int i = 0; i < remappings.size(); i++) {
				if (period() == r[i].src_period) {
					return r + i;
				}
			}
		}
		return 0;
	}

	// 45 = MIDI A3 (110 Hz)
	unsigned char midi_note_a() const { return 33; }

	void set_clock_rate(double clock_rate) {
		clock_rate_ = clock_rate;

		// dumb map to GM percussion notes by default:
		for (int i = 0; i < 16; i++) {
			period_midi[i] = 36 + i;
			period_midi[i+16] = 81 - i;
		}
	}

	unsigned char last_midi_note_volume;
	unsigned char midi_note_volume() const { return (unsigned char)(volume() * 8); }
	unsigned char midi_channel_volume() const { return 120; }
	unsigned char midi_channel() const { return 9; }

	unsigned char midi_note() const {
		Noise_Remapping *r = find_remapping();
		if (r != 0) {
			return r->dest_midi_note;
		}

		return period_midi[period()];
	}

	virtual void note_on(nes_time_t time) {
		unsigned char m = midi_note();

		if (m != last_midi_note || midi_note_volume() > last_midi_note_volume) {
			note_off(time);
			midi_write_note_on(time);
			last_midi_channel = midi_channel();
		}

		last_midi_note = m;
		last_midi_note_volume = midi_note_volume();
	}

	int period() const {
		const int mode_flag = 0x80;
		return (regs [2] & 15) | ((regs [2] & mode_flag) >> 3);
	}

	void run( nes_time_t, nes_time_t );
	void reset() {
		noise = 1 << 14;
		Nes_Envelope::reset();

		last_midi_note_volume = 0;
	}
};

struct Dmc_Remapping {
	int src_address;
	int src_midi_note;
	int dest_midi_chan;
	int dest_midi_note;
};

// Nes_Dmc
struct Nes_Dmc : Nes_Osc
{
	int address;    // address of next byte to read
	int period;
	//int length_counter; // bytes remaining to play (already defined in Nes_Osc)
	int buf;
	int bits_remain;
	int bits;
	bool buf_full;
	bool silence;
	
	enum { loop_flag = 0x40 };
	
	int dac;
	
	nes_time_t next_irq;
	bool irq_enabled;
	bool irq_flag;
	bool pal_mode;
	bool nonlinear;
	
	int (*prg_reader)( void*, nes_addr_t ); // needs to be initialized to prg read function
	void* prg_reader_data;
	
	Nes_Apu* apu;
	
	Blip_Synth<blip_med_quality,1> synth;

	mutable blargg_vector<int> channel_address_map;

	// 45 = MIDI A3 (110 Hz)
	unsigned char midi_note_a() const { return 33; }

	blargg_vector<Dmc_Remapping> remappings;
	Dmc_Remapping *find_remapping() const {
		Dmc_Remapping *r = remappings.begin();
		if (r != 0) {
			for (int i = 0; i < remappings.size(); i++) {
				if (regs[2] == r[i].src_address && period_midi[period] == r[i].src_midi_note) {
					return r + i;
				}
			}
		}
		return 0;
	}

	unsigned char midi_channel() const {
		Dmc_Remapping *r = find_remapping();
		if (r != 0) {
			return r->dest_midi_chan;
		}

		if (channel_address_map.begin() == 0) {
			channel_address_map.resize(6);
			for (int i = 0; i < 6; i++) {
				channel_address_map.begin()[i] = -1;
			}
		}

		// Find existing channel for address:
		int chan = -1;
		int free_slot = -1;
		int* p = channel_address_map.begin();
		for (int i = 0; i < 6; i++) {
			if (p[i] == regs[2]) {
				chan = i;
				break;
			}
			if (free_slot == -1 && p[i] == -1) {
				free_slot = i;
			}
		}

		if (chan == -1) {
			if (free_slot == -1) {
				printf("No free MIDI channels for DMC sample 0x%02X\n", regs[2]);
				return 15;
			} else {
				p[free_slot] = regs[2];
				chan = free_slot;
				printf("MIDI channel %d allocated for DMC sample 0x%02X\n", 10 + chan + 1, regs[2]);
			}
		}

		return 10 + chan;
	}
	unsigned char midi_note() const {
		Dmc_Remapping *r = find_remapping();
		if (r != 0) {
			return r->dest_midi_note;
		}

		return period_midi[period];
	}
	unsigned char midi_note_volume() const { return 64; }
	unsigned char midi_channel_volume() const { return 64; }

	void start(nes_time_t);
	void write_register( int, int );
	void run( nes_time_t, nes_time_t );
	void recalc_irq();
	void fill_buffer(nes_time_t);
	void reload_sample();
	void reset();
	int count_reads( nes_time_t, nes_time_t* ) const;
	nes_time_t next_read_time() const;
};

#endif
