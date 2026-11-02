#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include "tusb.h"

// UAC1 stereo microphone. Same shape as TinyUSB's one-channel template,
// with a left and a right channel on the input terminal and the feature unit.
// bcdADC is 0x0100. Full-speed only, which is all an RP2040 can speak.

#define TUD_AUDIO10_MIC_TWO_CH_DESC_LEN(_nfreqs) ( \
    TUD_AUDIO10_DESC_STD_AC_LEN \
    + TUD_AUDIO10_DESC_CS_AC_LEN(1) \
    + TUD_AUDIO10_DESC_INPUT_TERM_LEN \
    + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN \
    + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(2) \
    + TUD_AUDIO10_DESC_STD_AS_LEN \
    + TUD_AUDIO10_DESC_STD_AS_LEN \
    + TUD_AUDIO10_DESC_CS_AS_INT_LEN \
    + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(_nfreqs) \
    + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN \
    + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN)

#define TUD_AUDIO10_MIC_TWO_CH_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epin, _epsize, ...) \
  TUD_AUDIO10_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx), \
  TUD_AUDIO10_DESC_CS_AC(/*_bcdADC*/ 0x0100, \
      /*_totallen*/ (TUD_AUDIO10_DESC_INPUT_TERM_LEN + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(2)), \
      /*_itf*/ (uint8_t)((_itfnum) + 1)), \
  TUD_AUDIO10_DESC_INPUT_TERM(/*_termid*/ 0x01, /*_termtype*/ AUDIO_TERM_TYPE_IN_GENERIC_MIC, \
      /*_assocTerm*/ 0x03, /*_nchannels*/ 0x02, \
      /*_channelcfg*/ (AUDIO10_CHANNEL_CONFIG_LEFT_FRONT | AUDIO10_CHANNEL_CONFIG_RIGHT_FRONT), \
      /*_idxchannelnames*/ 0x00, /*_stridx*/ 0x00), \
  TUD_AUDIO10_DESC_OUTPUT_TERM(/*_termid*/ 0x03, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, \
      /*_assocTerm*/ 0x01, /*_srcid*/ 0x02, /*_stridx*/ 0x00), \
  TUD_AUDIO10_DESC_FEATURE_UNIT(/*_unitid*/ 0x02, /*_srcid*/ 0x01, /*_stridx*/ 0x00, \
      /*_ctrlmaster*/ (AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME), \
      /*_ctrlch1*/ (AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME), \
      /*_ctrlch2*/ (AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME)), \
  TUD_AUDIO10_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, /*_stridx*/ 0x00), \
  TUD_AUDIO10_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x01, /*_nEPs*/ 0x01, /*_stridx*/ 0x00), \
  TUD_AUDIO10_DESC_CS_AS_INT(/*_termid*/ 0x03, /*_delay*/ 0x01, /*_formattype*/ AUDIO10_DATA_FORMAT_TYPE_I_PCM), \
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(/*_nrchannels*/ 0x02, /*_subframesize*/ _nBytesPerSample, \
      /*_bitresolution*/ _nBitsUsedPerSample, /*_freq*/ __VA_ARGS__), \
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(/*_ep*/ _epin, \
      /*_attr*/ (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS), \
      /*_maxEPsize*/ _epsize, /*_interval*/ 0x01, /*_sync_ep*/ 0x00), \
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ, \
      /*_lockdelayunits*/ AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, /*_lockdelay*/ 0x0001)

#endif
