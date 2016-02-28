/* -*- c++ -*- */
/*
 * Copyright 2013-2015 Nuand LLC
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

#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <boost/lexical_cast.hpp>
#include <boost/assign.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>

#include "bladerf_common.h"

#define NUM_BUFFERS 32
#define NUM_SAMPLES_PER_BUFFER (4 * 1024)

using namespace boost::assign;

boost::mutex bladerf_common::_devs_mutex;
std::list<boost::weak_ptr<struct bladerf> > bladerf_common::_devs;

bladerf_common::bladerf_common() :
  _conv_buf(NULL),
  _conv_buf_size(4096),
  _xb_200_attached(false),
  _consecutive_failures(0)
{

}

bladerf_common::~bladerf_common()
{
    free(_conv_buf);
}

bladerf_sptr bladerf_common:: get_cached_device(struct bladerf_devinfo devinfo)
{
  /* Lock to _devs must be aquired by caller */
  BOOST_FOREACH( boost::weak_ptr<struct bladerf> dev, _devs )
  {
    struct bladerf_devinfo other_devinfo;

    int rv = bladerf_get_devinfo(bladerf_sptr(dev).get(), &other_devinfo);
    if (rv < 0)
      throw std::runtime_error(std::string(__FUNCTION__) + " " +
                               "Failed to get devinfo for cached device.");

    if (bladerf_devinfo_matches(&devinfo, &other_devinfo)) {
      return bladerf_sptr(dev);
    }
  }

  return bladerf_sptr();
}

/* This is called when a bladerf_sptr hits a refcount of 0 */
void bladerf_common::close(void* dev)
{
  boost::unique_lock<boost::mutex> lock(_devs_mutex);

  /* Prune expired entries from device cache */
  std::list<boost::weak_ptr<struct bladerf> >::iterator it(_devs.begin());
  while ( it != _devs.end() ) {
    if ( (*it).expired() ) {
      it = _devs.erase(it);
    } else {
      ++it;
    }
  }

  bladerf_close((struct bladerf *)dev);
}

bladerf_sptr bladerf_common::open(const std::string &device_name)
{
  int rv;
  struct bladerf *raw_dev;
  struct bladerf_devinfo devinfo;

  boost::unique_lock<boost::mutex> lock(_devs_mutex);

  rv = bladerf_get_devinfo_from_str(device_name.c_str(), &devinfo);
  if (rv < 0)
    throw std::runtime_error(std::string(__FUNCTION__) + " " +
                             "Failed to get devinfo for '" + device_name + "'");

  bladerf_sptr cached_dev = get_cached_device(devinfo);

  if (cached_dev)
    return cached_dev;

  rv = bladerf_open_with_devinfo(&raw_dev, &devinfo);
  if (rv < 0)
    throw std::runtime_error(std::string(__FUNCTION__) + " " +
                             "Failed to open device for '" + device_name + "'");

  bladerf_sptr dev = bladerf_sptr(raw_dev, bladerf_common::close);

  _devs.push_back(boost::weak_ptr<struct bladerf>(dev));

  return dev;
}

void bladerf_common::set_loopback_mode(const std::string &loopback)
{
    bladerf_loopback mode;
    int status;

    if (loopback == "bb_txlpf_rxvga2") {
        mode = BLADERF_LB_BB_TXLPF_RXVGA2;
    } else if (loopback == "bb_txlpf_rxlpf") {
        mode = BLADERF_LB_BB_TXLPF_RXLPF;
    } else if (loopback == "bb_txvga1_rxvga2") {
        mode = BLADERF_LB_BB_TXVGA1_RXVGA2;
    } else if (loopback == "bb_txvga1_rxlpf") {
        mode = BLADERF_LB_BB_TXVGA1_RXLPF;
    } else if (loopback == "rf_lna1") {
        mode = BLADERF_LB_RF_LNA1;
    } else if (loopback == "rf_lna2") {
        mode = BLADERF_LB_RF_LNA2;
    } else if (loopback == "rf_lna3") {
        mode = BLADERF_LB_RF_LNA3;
    } else if (loopback == "none") {
        mode = BLADERF_LB_NONE;
    } else {
        throw std::runtime_error( _pfx + "Invalid loopback mode:" + loopback );
    }

    status = bladerf_set_loopback( _dev.get(), mode);
    if ( status != 0 ) {
        throw std::runtime_error( _pfx + "Failed to set loopback mode: " +
                                  bladerf_strerror(status) );
    }
}

void bladerf_common::set_verbosity(const std::string &verbosity)
{
    bladerf_log_level l;

    if (verbosity == "verbose") {
        l = BLADERF_LOG_LEVEL_VERBOSE;
    } else if (verbosity == "debug") {
        l = BLADERF_LOG_LEVEL_DEBUG;
    } else if (verbosity == "info") {
        l = BLADERF_LOG_LEVEL_INFO;
    } else if (verbosity == "warning") {
        l = BLADERF_LOG_LEVEL_WARNING;
    } else if (verbosity == "error") {
        l = BLADERF_LOG_LEVEL_ERROR;
    } else if (verbosity == "critical") {
        l = BLADERF_LOG_LEVEL_CRITICAL;
    } else if (verbosity == "silent") {
        l = BLADERF_LOG_LEVEL_SILENT;
    } else {
        throw std::runtime_error( _pfx + "Invalid log level: " + verbosity );
    }

    bladerf_log_set_verbosity(l);
}

bool bladerf_common::start(bladerf_module module)
{
  int ret;
  bladerf_format format;

  if (_use_metadata) {
      format = BLADERF_FORMAT_SC16_Q11_META;
  } else {
      format = BLADERF_FORMAT_SC16_Q11;
  }

  ret = bladerf_sync_config(_dev.get(), module, format,
                            _num_buffers, _samples_per_buffer,
                            _num_transfers, _stream_timeout_ms);

  if ( ret != 0 ) {
    std::cerr << _pfx << "bladerf_sync_config failed: "
              << bladerf_strerror(ret) << std::endl;
    return false;
  }

  ret = bladerf_enable_module(_dev.get(), module, true);
  if ( ret != 0 ) {
    std::cerr << _pfx << "bladerf_enable_module failed: "
              << bladerf_strerror(ret) << std::endl;
    return false;
  }

  return true;
}

bool bladerf_common::stop(bladerf_module module)
{
  int ret;

  ret = bladerf_enable_module(_dev.get(), module, false);

  if ( ret != 0 ) {
    std::cerr << _pfx << "bladerf_enable_modue failed: "
              << bladerf_strerror(ret) << std::endl;
    return false;
  }

  return true;
}

static bool version_greater_or_equal(const struct bladerf_version *version,
                                    unsigned int major, unsigned int minor,
                                    unsigned int patch)
{
    if (version->major > major) {
        return true;
    } else if ( (version->major == major) && (version->minor > minor) ) {
        return true;
    } else if ((version->major == major) &&
               (version->minor == minor) &&
               (version->patch >= patch) ) {
        return true;
    } else {
        return false;
    }
}

void bladerf_common::init(dict_t &dict, bladerf_module module)
{
  int ret;
  std::string device_name("");
  struct bladerf_version ver;
  char serial[BLADERF_SERIAL_LENGTH];
  const char *type = (module == BLADERF_MODULE_TX ? "sink" : "source");

  _pfx = std::string("[bladeRF ") + std::string(type) + std::string("] ");

  if ( dict.count("verbosity") )
    set_verbosity( dict["verbosity"] );

  if (dict.count("bladerf"))
  {
    const std::string value = dict["bladerf"];
    if ( value.length() > 0)
    {

      if ( value.length() <= 2 )
      {
        /* If the value is two digits or less, we'll assume the user is
         * providing an instance number */
        unsigned int device_number = 0;

        try {
          device_number = boost::lexical_cast< unsigned int >( value );
          device_name = boost::str(boost::format( "*:instance=%d" ) % device_number);
        } catch ( std::exception &ex ) {
          throw std::runtime_error( _pfx + "Failed to use '" + value +
                                   "' as device number: " + ex.what());
        }

      } else {
        /* Otherwise, we'll assume it's a serial number. libbladeRF v1.4.1
         * supports matching a subset of a serial number. For earlier versions,
         * we require the entire serial number.
         *
         * libbladeRF is responsible for rejecting bad serial numbers, so we
         * may just pass whatever the user has provided.
         */
        bladerf_version(&ver);
        if ( version_greater_or_equal(&ver, 1, 4, 1) ||
             value.length() == (BLADERF_SERIAL_LENGTH - 1) )
        {
          device_name = std::string("*:serial=") + value;
        } else {
          throw std::runtime_error( _pfx + "A full serial number must be " +
                                    "supplied with libbladeRF " +
                                    std::string(ver.describe) +
                                    ". libbladeRF >= v1.4.1 supports opening " +
                                    "a device via a subset of its serial #.");
        }
      }
    }
  }

  try {
    std::cerr << "Opening nuand bladeRF with device identifier string: \""
              << device_name << "\"" << std::endl;

    _dev = open(device_name);
  } catch(...) {
    throw std::runtime_error( _pfx + "Failed to open bladeRF device " +
                              device_name );
  }

  /* Load an FPGA */
  if ( dict.count("fpga") )
  {

    if ( dict.count("fpga-reload") == 0 &&
         bladerf_is_fpga_configured( _dev.get() ) == 1 ) {

      std::cerr << _pfx << "FPGA is already loaded. Set fpga-reload=1 "
                << "to force a reload." << std::endl;

    } else {
      std::string fpga = dict["fpga"];

      std::cerr << _pfx << "Loading FPGA bitstream " << fpga << "..." << std::endl;
      ret = bladerf_load_fpga( _dev.get(), fpga.c_str() );
      if ( ret != 0 )
        std::cerr << _pfx << "bladerf_load_fpga has failed with " << ret << std::endl;
      else
        std::cerr << _pfx << "The FPGA bitstream has been successfully loaded." << std::endl;
    }
  }

  if ( bladerf_is_fpga_configured( _dev.get() ) != 1 )
  {
    std::ostringstream oss;
    oss << _pfx << "The FPGA is not configured! "
        << "Provide device argument fpga=/path/to/the/bitstream.rbf to load it.";

    throw std::runtime_error( oss.str() );
  }

  if ( module == BLADERF_MODULE_RX )
  {
      if ( dict.count("loopback") )
          set_loopback_mode( dict["loopback"] );
      else
          set_loopback_mode( "none" );
  }
  else if ( module == BLADERF_MODULE_TX && dict.count("loopback") )
  {
      std::cerr << _pfx
                << "Warning: 'loopback' has been specified on a bladeRF sink, "
                   "and will have no effect. This parameter should be "
                   "specified on the associated bladeRF source."
                << std::endl;
  }

  if ( dict.count("xb200") ) {
    if (bladerf_expansion_attach(_dev.get(), BLADERF_XB_200)) {
      std::cerr << _pfx << "Could not attach XB-200" << std::endl;
    } else {
      _xb_200_attached = true;

      bladerf_xb200_filter filter = BLADERF_XB200_AUTO_1DB;

      if ( dict["xb200"] == "custom" ) {
        filter = BLADERF_XB200_CUSTOM;
      } else if ( dict["xb200"] == "50M" ) {
        filter = BLADERF_XB200_50M;
      } else if ( dict["xb200"] == "144M" ) {
        filter = BLADERF_XB200_144M;
      } else if ( dict["xb200"] == "222M" ) {
        filter = BLADERF_XB200_222M;
      } else if ( dict["xb200"] == "auto3db" ) {
        filter = BLADERF_XB200_AUTO_3DB;
      } else if ( dict["xb200"] == "auto" ) {
        filter = BLADERF_XB200_AUTO_1DB;
      } else {
        filter = BLADERF_XB200_AUTO_1DB;
      }

      if (bladerf_xb200_set_filterbank(_dev.get(), module, filter)) {
          std::cerr << _pfx << "Could not set XB-200 filter" << std::endl;
      }
    }
  }

  /* Show some info about the device we've opened */

  if ( bladerf_get_serial( _dev.get(), serial ) == 0 )
  {
    std::string strser(serial);

    if ( strser.length() == 32 )
      strser.replace( 4, 24, "..." );

    std::cerr << " Serial # " << strser << std::endl;
  }

  if ( bladerf_fw_version( _dev.get(), &ver ) == 0 )
    std::cerr << " FW v" << ver.major << "." << ver.minor << "." << ver.patch;

  if ( bladerf_fpga_version( _dev.get(), &ver ) == 0 )
    std::cerr << " FPGA v" << ver.major << "." << ver.minor << "." << ver.patch;

  std::cerr << std::endl;

  if (dict.count("tamer")) {
    set_clock_source( dict["tamer"] );
    std::cerr << _pfx << "Tamer mode set to '" << get_clock_source() << "'";
  }

  if (dict.count("smb")) {
    set_smb_frequency( boost::lexical_cast< double >( dict["smb"] ) );
    std::cerr << _pfx << "SMB frequency set to " << get_smb_frequency() << " Hz";
  }

  /* Initialize buffer and sample configuration */
  _num_buffers = 0;
  if (dict.count("buffers")) {
    _num_buffers = boost::lexical_cast< size_t >( dict["buffers"] );
  }

  _samples_per_buffer = 0;
  if (dict.count("buflen")) {
    _samples_per_buffer = boost::lexical_cast< size_t >( dict["buflen"] );
  }

  _num_transfers = 0;
  if (dict.count("transfers")) {
    _num_transfers = boost::lexical_cast< size_t >( dict["transfers"] );
  }

  _stream_timeout_ms = 3000;
  if (dict.count("stream_timeout_ms")) {
      _stream_timeout_ms = boost::lexical_cast< unsigned int >(dict["stream_timeout_ms"] );
  }

  _use_metadata = dict.count("enable_metadata") != 0;

  /* Require value to be >= 2 so we can ensure we have twice as many
   * buffers as transfers */
  if (_num_buffers <= 1) {
    _num_buffers = NUM_BUFFERS;
  }

  if (0 == _samples_per_buffer) {
    _samples_per_buffer = NUM_SAMPLES_PER_BUFFER;
  } else {
    if (_samples_per_buffer < 1024 || _samples_per_buffer % 1024 != 0) {

      /* 0 likely implies the user did not specify this, so don't warn */
      if (_samples_per_buffer != 0 ) {
        std::cerr << _pfx << "Invalid \"buflen\" value. "
                  << "A multiple of 1024 is required. Defaulting to "
                  << NUM_SAMPLES_PER_BUFFER << std::endl;
      }

      _samples_per_buffer = NUM_SAMPLES_PER_BUFFER;
    }
  }

  /* If the user hasn't specified the desired number of transfers, set it to
   * min(32, num_buffers / 2) */
  if (_num_transfers == 0) {
      _num_transfers = _num_buffers / 2;
      if (_num_transfers > 32) {
          _num_transfers = 32;
      }
  } else if (_num_transfers >= _num_buffers) {
      _num_transfers = _num_buffers - 1;
      std::cerr << _pfx << "Clamping num_tranfers to " << _num_transfers << ". "
                << "Try using a smaller num_transfers value if timeouts occur."
                << std::endl;
  }

  _conv_buf = static_cast<int16_t*>(malloc(_conv_buf_size * 2 * sizeof(int16_t)));

  if (_conv_buf == NULL) {
    throw std::runtime_error( std::string(__FUNCTION__) +
                              "Failed to allocate _conv_buf" );
  }
}

osmosdr::freq_range_t bladerf_common::freq_range()
{
  /* assuming the same for RX & TX */
  return osmosdr::freq_range_t( _xb_200_attached ? 0 : 280e6,  BLADERF_FREQUENCY_MAX );
}

osmosdr::meta_range_t bladerf_common::sample_rates()
{
  osmosdr::meta_range_t sample_rates;

  /* assuming the same for RX & TX */
  sample_rates += osmosdr::range_t( 160e3, 200e3, 40e3 );
  sample_rates += osmosdr::range_t( 300e3, 900e3, 100e3 );
  sample_rates += osmosdr::range_t( 1e6, 40e6, 1e6 );

  return sample_rates;
}

osmosdr::freq_range_t bladerf_common::filter_bandwidths()
{
  /* the same for RX & TX according to the datasheet */
  osmosdr::freq_range_t bandwidths;

  std::vector<double> half_bandwidths; /* in MHz */
  half_bandwidths += \
      0.75, 0.875, 1.25, 1.375, 1.5, 1.92, 2.5,
      2.75, 3, 3.5, 4.375, 5, 6, 7, 10, 14;

  BOOST_FOREACH( double half_bw, half_bandwidths )
    bandwidths += osmosdr::range_t( half_bw * 2e6 );

  return bandwidths;
}

std::vector< std::string > bladerf_common::devices()
{
  struct bladerf_devinfo *devices;
  ssize_t n_devices;
  std::vector< std::string > ret;

  n_devices = bladerf_get_device_list(&devices);

  if (n_devices > 0)
  {
    for (ssize_t i = 0; i < n_devices; i++)
    {
      std::stringstream s;
      std::string serial(devices[i].serial);

      s << "bladerf=" << devices[i].instance << ","
        << "label='nuand bladeRF";

      if ( serial.length() == 32 )
        serial.replace( 4, 24, "..." );

      if ( serial.length() )
        s << " SN " << serial;

      s << "'";

      ret.push_back(s.str());
    }

    bladerf_free_device_list(devices);
  }

  return ret;
}

double bladerf_common::set_sample_rate( bladerf_module module, double rate )
{
  int status;
  struct bladerf_rational_rate rational_rate, actual;

  rational_rate.integer = (uint32_t)rate;
  rational_rate.den = 10000;
  rational_rate.num = (rate - rational_rate.integer) * rational_rate.den;

  status = bladerf_set_rational_sample_rate( _dev.get(), module,
                                             &rational_rate, &actual );

  if ( status != 0 ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "Failed to set integer rate:" +
                              std::string(bladerf_strerror(status)));
  }

  return actual.integer + actual.num / (double)actual.den;
}

double bladerf_common::get_sample_rate( bladerf_module module )
{
  int status;
  double ret = 0.0;
  struct bladerf_rational_rate rate;


  status = bladerf_get_rational_sample_rate( _dev.get(), module, &rate );

  if ( status != 0 ) {
   throw std::runtime_error( std::string(__FUNCTION__) +
                             "Failed to get sample rate:" +
                             std::string(bladerf_strerror(status)) );
  } else {
    ret = rate.integer + rate.num / (double)rate.den;
  }

  return ret;
}

int bladerf_common::set_dc_offset(bladerf_module module, const std::complex<double> &offset, size_t chan)
{
    int ret = 0;
    int16_t val_i, val_q;

    val_i = (int16_t)(offset.real() * DCOFF_SCALE);
    val_q = (int16_t)(offset.imag() * DCOFF_SCALE);

    ret  = bladerf_set_correction(_dev.get(), module, BLADERF_CORR_LMS_DCOFF_I, val_i);
    ret |= bladerf_set_correction(_dev.get(), module, BLADERF_CORR_LMS_DCOFF_Q, val_q);

    return ret;
}

int bladerf_common::set_iq_balance(bladerf_module module, const std::complex<double> &balance, size_t chan)
{
    int ret = 0;
    int16_t val_gain, val_phase;

    val_gain = (int16_t)(balance.real() * GAIN_SCALE);
    val_phase = (int16_t)(balance.imag() * PHASE_SCALE);

    ret  = bladerf_set_correction(_dev.get(), module, BLADERF_CORR_FPGA_GAIN, val_gain);
    ret |= bladerf_set_correction(_dev.get(), module, BLADERF_CORR_FPGA_PHASE, val_phase);

    return ret;
}

void bladerf_common::set_clock_source(const std::string &source, const size_t mboard)
{
  bladerf_vctcxo_tamer_mode tamer_mode = BLADERF_VCTCXO_TAMER_DISABLED;

  std::vector<std::string> clock_sources = get_clock_sources(mboard);

  int index = std::find(clock_sources.begin(), clock_sources.end(), source) - clock_sources.begin();

  if ( index < int(clock_sources.size()) ) {
    tamer_mode = static_cast<bladerf_vctcxo_tamer_mode>(index);
  }

  int status = bladerf_set_vctcxo_tamer_mode( _dev.get(), tamer_mode );
  if ( status != 0 )
    throw std::runtime_error(_pfx + "Failed to set VCTCXO tamer mode: " +
                             bladerf_strerror(status));
}

std::string bladerf_common::get_clock_source(const size_t mboard)
{
  bladerf_vctcxo_tamer_mode tamer_mode = BLADERF_VCTCXO_TAMER_INVALID;

  int status = bladerf_get_vctcxo_tamer_mode( _dev.get(), &tamer_mode );
  if ( status != 0 )
    throw std::runtime_error(_pfx + "Failed to get VCTCXO tamer mode: " +
                             bladerf_strerror(status));

  std::vector<std::string> clock_sources = get_clock_sources(mboard);

  return clock_sources.at(tamer_mode);
}

std::vector<std::string> bladerf_common::get_clock_sources(const size_t mboard)
{
  std::vector<std::string> sources;

  // assumes zero-based 1:1 mapping
  sources.push_back("internal");      // BLADERF_VCTCXO_TAMER_DISABLED
  sources.push_back("external_1pps"); // BLADERF_VCTCXO_TAMER_1_PPS
  sources.push_back("external");      // BLADERF_VCTCXO_TAMER_10_MHZ

  return sources;
}

void bladerf_common::set_smb_frequency(double frequency)
{
  uint32_t actual_frequency = frequency;

  int status = bladerf_set_smb_frequency( _dev.get(), uint32_t(frequency), &actual_frequency );
  if ( status != 0 )
    throw std::runtime_error(_pfx + "Failed to set SMB frequency: " +
                             bladerf_strerror(status));

  if ( uint32_t(frequency) != actual_frequency )
    std::cerr << _pfx << "Wanted SMB frequency is " << frequency
              << ", actual is " << actual_frequency
              << std::endl;
}

double bladerf_common::get_smb_frequency()
{
  unsigned int actual_frequency;

  int status = bladerf_get_smb_frequency( _dev.get(), &actual_frequency );
  if ( status != 0 )
    throw std::runtime_error(_pfx + "Failed to get SMB frequency: " +
                             bladerf_strerror(status));

  return actual_frequency;
}
