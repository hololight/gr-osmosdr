/* -*- c++ -*- */
/*
 * Copyright 2013 Nuand LLC
 * Copyright 2013 Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>

#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <gnuradio/io_signature.h>
#include <gnuradio/tags.h>
#include <gnuradio/sync_block.h>

#include "arg_helpers.h"
#include "bladerf_sink_c.h"

//#define DEBUG_BLADERF_SINK
#ifdef DEBUG_BLADERF_SINK
#   define DBG(input)  std::cerr << _pfx << input << std::endl
#else
#   define DBG(input)
#endif

using namespace boost::assign;

/*
 * Create a new instance of bladerf_sink_c and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
bladerf_sink_c_sptr make_bladerf_sink_c (const std::string &args)
{
  return gnuradio::get_initial_sptr(new bladerf_sink_c (args));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept
 * only 0 input and 1 output.
 */
static const int MIN_IN = 1;   // mininum number of input streams
static const int MAX_IN = 1;   // maximum number of input streams
static const int MIN_OUT = 0;  // minimum number of output streams
static const int MAX_OUT = 0;  // maximum number of output streams

/*
 * The private constructor
 */
bladerf_sink_c::bladerf_sink_c (const std::string &args)
  : gr::sync_block ("bladerf_sink_c",
                    gr::io_signature::make (MIN_IN, MAX_IN, sizeof (gr_complex)),
                    gr::io_signature::make (MIN_OUT, MAX_OUT, sizeof (gr_complex)))
{
  dict_t dict = params_to_dict(args);

  /* Perform src/sink agnostic initializations */
  init(dict, BLADERF_MODULE_TX);

  /* Set the range of VGA1, VGA1GAINT[7:0] */
  _vga1_range = osmosdr::gain_range_t( -35, -4, 1 );

  /* Set the range of VGA2, VGA2GAIN[4:0] */
  _vga2_range = osmosdr::gain_range_t( 0, 25, 1 );

}

bool bladerf_sink_c::start()
{
  _in_burst = false;
  return bladerf_common::start(BLADERF_MODULE_TX);
}

bool bladerf_sink_c::stop()
{
  return bladerf_common::stop(BLADERF_MODULE_TX);
}

#define INVALID_IDX -1

int bladerf_sink_c::transmit_with_tags(int noutput_items)
{
  int count = 0;
  int status = 0;

  // For a long burst, we may be transmitting the burst contents over
  // multiple work calls, so we'll just be sending the entire buffer
  // Therefore, we initialize our indicies for this case.
  int start_idx = 0;
  int end_idx = (noutput_items - 1);

  struct bladerf_metadata meta;
  std::vector<gr::tag_t> tags;

  int16_t zeros[8] = { 0 };

  memset(&meta, 0, sizeof(meta));

  DBG("transmit_with_tags(" << noutput_items << ")");

  // Important Note: We assume that these tags are ordered by their offsets.
  // This is true for GNU Radio 3.7.7.x, since the GR runtime libs store
  // these in a multimap.
  //
  // If you're using an earlier GNU Radio version, you may have to sort
  // the tags vector.
  get_tags_in_window(tags, 0, 0, noutput_items);

  if (tags.size() == 0) {
    if (_in_burst) {
      DBG("TX'ing " << noutput_items << " samples in within a burst...");

      return bladerf_sync_tx(_dev.get(),
                             static_cast<void *>(_conv_buf),
                             noutput_items, &meta, _stream_timeout_ms);
    } else {
        std::cerr << _pfx << "Dropping " << noutput_items
                  << " samples not in a burst." << std::endl;
    }
  }


  BOOST_FOREACH( gr::tag_t tag, tags) {

    // Upon seeing an SOB tag, update our offset. We'll TX the start of the
    // burst when we see an EOB or at the end of this function - whichever
    // occurs first.
    if (pmt::symbol_to_string(tag.key) == "tx_sob") {
      if (_in_burst) {
        std::cerr << ("Got SOB while already within a burst");
        return BLADERF_ERR_INVAL;
      } else {
        start_idx = static_cast<int>(tag.offset - nitems_read(0));
        DBG("Got SOB " << start_idx << " samples into work payload");

        meta.flags |= (BLADERF_META_FLAG_TX_NOW | BLADERF_META_FLAG_TX_BURST_START);
        _in_burst = true;

      }
    } else if (pmt::symbol_to_string(tag.key) == "tx_eob") {
      if (!_in_burst) {
        std::cerr << _pfx << "Got EOB while not in burst" << std::endl;
        return BLADERF_ERR_INVAL;
      }

      // Upon seeing an EOB, transmit what we have and reset our state
      end_idx = static_cast<int>(tag.offset - nitems_read(0));
      DBG("Got EOB " << end_idx << " samples into work payload");

      if ( (start_idx == INVALID_IDX) || (start_idx > end_idx) ) {
        DBG("Buffer indicies are in an invalid state!");
        return BLADERF_ERR_INVAL;
      }

      count = end_idx - start_idx + 1;

      DBG("TXing @ EOB [" << start_idx << ":" << end_idx << "]");

      status = bladerf_sync_tx(_dev.get(),
                               static_cast<void *>(&_conv_buf[2*start_idx]),
                               count, &meta, _stream_timeout_ms);
      if (status != 0) {
        return status;
      }

      /* TODO: libbladeRF should now take care of this for us,
       *       as of the libbladeRF version that includes the
       *       TX_UPDATE_TIMESTAMP flag.  Verify this potentially remove this.
       *       (The meta.flags changes would then be applied to the previous
       *        bladerf_sync_tx() call.)
       */
      DBG("TXing Zeros with burst end flag");

      meta.flags &= ~(BLADERF_META_FLAG_TX_NOW | BLADERF_META_FLAG_TX_BURST_START);
      meta.flags |= BLADERF_META_FLAG_TX_BURST_END;

      status = bladerf_sync_tx(_dev.get(),
                               static_cast<void *>(zeros),
                               4, &meta, _stream_timeout_ms);


      /* Reset our state */
      start_idx = INVALID_IDX;
      end_idx = (noutput_items - 1);
      meta.flags = 0;
      _in_burst = false;

      if (status != 0) {
        DBG("Failed to send zero samples to flush EOB");
        return status;
      }
    }
  }

  // We had a start of burst with no end yet - transmit those samples
  if (_in_burst) {
      count = end_idx - start_idx + 1;

      DBG("TXing SOB [" << start_idx << ":" << end_idx << "]");

      status = bladerf_sync_tx(_dev.get(),
                               static_cast<void *>(&_conv_buf[2*start_idx]),
                               count, &meta, _stream_timeout_ms);
  }

  return status;
}

int bladerf_sink_c::work( int noutput_items,
                          gr_vector_const_void_star &input_items,
                          gr_vector_void_star &output_items )
{
  const gr_complex *in = (const gr_complex *) input_items[0];
  const float scaling = 2000.0f;
  int ret;

  if (noutput_items > _conv_buf_size) {
    void *tmp;

    _conv_buf_size = noutput_items;
    tmp = realloc(_conv_buf, _conv_buf_size * 2 * sizeof(int16_t));
    if (tmp == NULL) {
      throw std::runtime_error( std::string(__FUNCTION__) +
                                "Failed to realloc _conv_buf" );
    } else {
      DBG("Resized _conv_buf to " << _conv_buf_size << " samples");
    }

    _conv_buf = static_cast<int16_t*>(tmp);
  }

  /* Convert floating point samples into fixed point */
  for (int i = 0; i < 2 * noutput_items;) {
    _conv_buf[i++] = (int16_t)(scaling * real(*in));
    _conv_buf[i++] = (int16_t)(scaling * imag(*in++));
  }

  if (_use_metadata) {
    ret = transmit_with_tags(noutput_items);
  } else {
    ret = bladerf_sync_tx(_dev.get(), static_cast<void *>(_conv_buf),
                          noutput_items, NULL, _stream_timeout_ms);
  }

  if ( ret != 0 ) {
    std::cerr << _pfx << "bladerf_sync_tx error: "
              << bladerf_strerror(ret) << std::endl;

    _consecutive_failures++;

    if ( _consecutive_failures >= MAX_CONSECUTIVE_FAILURES ) {
        noutput_items = WORK_DONE;
        std::cerr << _pfx
                  << "Consecutive error limit hit. Shutting down."
                  << std::endl;
    }
  } else {
    _consecutive_failures = 0;
  }

  return noutput_items;
}


std::vector<std::string> bladerf_sink_c::get_devices()
{
  return bladerf_common::devices();
}

size_t bladerf_sink_c::get_num_channels()
{
  /* We only support a single channel for each bladeRF */
  return 1;
}

osmosdr::meta_range_t bladerf_sink_c::get_sample_rates()
{
  return sample_rates();
}

double bladerf_sink_c::set_sample_rate(double rate)
{
  return bladerf_common::set_sample_rate(BLADERF_MODULE_TX, rate);
}

double bladerf_sink_c::get_sample_rate()
{
  return bladerf_common::get_sample_rate(BLADERF_MODULE_TX);
}

osmosdr::freq_range_t bladerf_sink_c::get_freq_range( size_t chan )
{
  return freq_range();
}

double bladerf_sink_c::set_center_freq( double freq, size_t chan )
{
  int ret;

  /* Check frequency range */
  if( freq < get_freq_range( chan ).start() ||
      freq > get_freq_range( chan ).stop() ) {
    std::cerr << "Failed to set out of bound frequency: " << freq << std::endl;
  } else {
    ret = bladerf_set_frequency( _dev.get(), BLADERF_MODULE_TX, (uint32_t)freq );
    if( ret ) {
      throw std::runtime_error( std::string(__FUNCTION__) + " " +
                                "Failed to set center frequency " +
                                boost::lexical_cast<std::string>(freq) +
                                ":" + std::string(bladerf_strerror(ret)));
    }
  }

  return get_center_freq( chan );
}

double bladerf_sink_c::get_center_freq( size_t chan )
{
  uint32_t freq;
  int ret;

  ret = bladerf_get_frequency( _dev.get(), BLADERF_MODULE_TX, &freq );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "Failed to get center frequency:" +
                              std::string(bladerf_strerror(ret)));
  }

  return (double)freq;
}

double bladerf_sink_c::set_freq_corr( double ppm, size_t chan )
{
  /* TODO: Write the VCTCXO with a correction value (also changes RX ppm value!) */
  return get_freq_corr( chan );
}

double bladerf_sink_c::get_freq_corr( size_t chan )
{
  /* TODO: Return back the frequency correction in ppm */
  return 0;
}

std::vector<std::string> bladerf_sink_c::get_gain_names( size_t chan )
{
  std::vector< std::string > names;

  names += "VGA1", "VGA2";

  return names;
}

osmosdr::gain_range_t bladerf_sink_c::get_gain_range( size_t chan )
{
  /* TODO: This is an overall system gain range. Given the VGA1 and VGA2
  how much total gain can we have in the system */
  return get_gain_range( "VGA2", chan ); /* we use only VGA2 here for now */
}

osmosdr::gain_range_t bladerf_sink_c::get_gain_range( const std::string & name, size_t chan )
{
  osmosdr::gain_range_t range;

  if( name == "VGA1" ) {
    range = _vga1_range;
  } else if( name == "VGA2" ) {
    range = _vga2_range;
  } else {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "Requested an invalid gain element " + name );
  }

  return range;
}

bool bladerf_sink_c::set_gain_mode( bool automatic, size_t chan )
{
  return false;
}

bool bladerf_sink_c::get_gain_mode( size_t chan )
{
  return false;
}

double bladerf_sink_c::set_gain( double gain, size_t chan )
{
  return set_gain( gain, "VGA2", chan ); /* we use only VGA2 here for now */
}

double bladerf_sink_c::set_gain( double gain, const std::string & name, size_t chan)
{
  int ret = 0;

  if( name == "VGA1" ) {
    ret = bladerf_set_txvga1( _dev.get(), (int)gain );
  } else if( name == "VGA2" ) {
    ret = bladerf_set_txvga2( _dev.get(), (int)gain );
  } else {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "Requested to set the gain " +
                              "of an unknown gain element " + name );
  }

  /* Check for errors */
  if( ret ) {
    throw std::runtime_error(std::string(__FUNCTION__) + " " +
                             "Could not set " + name + " gain, error " +
                             std::string(bladerf_strerror(ret)));
  }

  return get_gain( name, chan );
}

double bladerf_sink_c::get_gain( size_t chan )
{
  return get_gain( "VGA2", chan ); /* we use only VGA2 here for now */
}

double bladerf_sink_c::get_gain( const std::string & name, size_t chan )
{
  int g;
  int ret = 0;

  if( name == "VGA1" ) {
    ret = bladerf_get_txvga1( _dev.get(), &g );
  } else if( name == "VGA2" ) {
    ret = bladerf_get_txvga2( _dev.get(), &g );
  } else {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "Requested to get the gain " +
                              "of an unknown gain element " + name );
  }

  /* Check for errors */
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "Could not get " + name + " gain, error " +
                              std::string(bladerf_strerror(ret)));
  }

  return (double)g;
}

double bladerf_sink_c::set_bb_gain( double gain, size_t chan )
{
  /* for TX, only VGA1 is in the BB path */
  osmosdr::gain_range_t bb_gains = get_gain_range( "VGA1", chan );

  double clip_gain = bb_gains.clip( gain, true );
  gain = set_gain( clip_gain, "VGA1", chan );

  return gain;
}

std::vector< std::string > bladerf_sink_c::get_antennas( size_t chan )
{
  std::vector< std::string > antennas;

  antennas += get_antenna( chan );

  return antennas;
}

std::string bladerf_sink_c::set_antenna( const std::string & antenna, size_t chan )
{
  return get_antenna( chan );
}

std::string bladerf_sink_c::get_antenna( size_t chan )
{
  /* We only have a single transmit antenna here */
  return "TX";
}

void bladerf_sink_c::set_dc_offset( const std::complex<double> &offset, size_t chan )
{
  int ret = 0;

  ret = bladerf_common::set_dc_offset(BLADERF_MODULE_TX, offset, chan);

  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set dc offset: " +
                              std::string(bladerf_strerror(ret)) );
  }
}

void bladerf_sink_c::set_iq_balance( const std::complex<double> &balance, size_t chan )
{
  int ret = 0;

  ret = bladerf_common::set_iq_balance(BLADERF_MODULE_TX, balance, chan);

  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set iq balance: " +
                              std::string(bladerf_strerror(ret)) );
  }
}

double bladerf_sink_c::set_bandwidth( double bandwidth, size_t chan )
{
  int ret;
  uint32_t actual;

  if ( bandwidth == 0.0 ) /* bandwidth of 0 means automatic filter selection */
    bandwidth = get_sample_rate() * 0.75; /* select narrower filters to prevent aliasing */

  ret = bladerf_set_bandwidth( _dev.get(), BLADERF_MODULE_TX, (uint32_t)bandwidth, &actual );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set bandwidth:" +
                              std::string(bladerf_strerror(ret)) );
  }

  return get_bandwidth();
}

double bladerf_sink_c::get_bandwidth( size_t chan )
{
  uint32_t bandwidth;
  int ret;

  ret = bladerf_get_bandwidth( _dev.get(), BLADERF_MODULE_TX, &bandwidth );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not get bandwidth: " +
                              std::string(bladerf_strerror(ret)) );
  }

  return (double)bandwidth;
}

osmosdr::freq_range_t bladerf_sink_c::get_bandwidth_range( size_t chan )
{
  return filter_bandwidths();
}

void bladerf_sink_c::set_clock_source(const std::string &source, const size_t mboard)
{
  bladerf_common::set_clock_source(source, mboard);
}

std::string bladerf_sink_c::get_clock_source(const size_t mboard)
{
  return bladerf_common::get_clock_source(mboard);
}

std::vector<std::string> bladerf_sink_c::get_clock_sources(const size_t mboard)
{
  return bladerf_common::get_clock_sources(mboard);
}
