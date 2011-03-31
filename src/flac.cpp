/*
 *      Copyright (C) 2009 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "xbmc_ac_types.h"
#include "xbmc_ac_dll.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> /* for stat() */

extern "C"
{

#include "FLAC/all.h"

struct FLAC
{
public:
  FLAC__StreamDecoder* decoder;
  BYTE* buffer;                         //  buffer to hold the decoded audio data
  unsigned int buffersize;              //  size of buffer is filled with decoded audio data
  unsigned int maxframesize;            //  size of a single decoded frame                 
  FLAC():
    decoder(NULL), buffer(NULL), buffersize(0), maxframesize(0)
  {}
};

FLAC__StreamDecoderWriteStatus DecoderWriteCallback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);
void DecoderMetadataCallback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
void DecoderErrorCallback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);
void FreeDecoder(AC_INFO *info);

//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS Create(void* hdl, void* props)
{
  //if (!props)
    return STATUS_OK;

  //return STATUS_NEED_SETTINGS;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void Destroy()
{
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool HasSettings()
{
  return false;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS GetStatus()
{
  return STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int GetSettings(StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS SetSetting(const char *strSetting, const void* value)
{
  return STATUS_OK;
}

AC_INFO* Init(const char* strFile, int track)
{
  AC_INFO* info = new AC_INFO();
  FLAC* mod = new FLAC();
  info->mod = mod;
  strcpy(info->name, "FLAC");

  mod->decoder = FLAC__stream_decoder_new();
  if (! info->mod) {
    DeInit(info);
    return NULL;
  }

  if (FLAC__stream_decoder_init_file(mod->decoder, strFile, DecoderWriteCallback,
    DecoderMetadataCallback, DecoderErrorCallback, info) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
  {
    DeInit(info);
    return NULL;
  }

  if (! FLAC__stream_decoder_process_until_end_of_metadata(mod->decoder))
  {
    DeInit(info);
    return NULL;
  }

  //  These are filled by the metadata callback
  if (info->samplerate == 0 || info->channels == 0 || info->bitpersample == 0
    || info->totaltime == 0 || mod->maxframesize == 0)
  {
    DeInit(info);
    return NULL;
  }

  //  Extract ReplayGain info
  /*CFlacTag tag;
  if (tag.Read(strFile))
    m_replayGain=tag.GetReplayGain();*/
  struct stat filestats;
  if(stat(strFile, &filestats) != 0)
  {
    DeInit(info);
    return NULL;
  } else
    info->bitrate = (int)(((float)filestats.st_size * 8) / ((float)info->totaltime / 1000));

  if (mod->buffer)
  {
    delete[] mod->buffer;
    mod->buffer = NULL;
  }

  //  allocate the buffer to hold the audio data,
  //  it is 5 times bigger then a single decoded frame
  mod->buffer = new BYTE[mod->maxframesize * 5];

  return info;
}

void DeInit(AC_INFO* info)
{
  FreeDecoder(info);
  if (info && info->mod)
  {
    delete[] ((FLAC *) info->mod)->buffer;
    ((FLAC *) info->mod)->buffer = NULL;
    delete (FLAC *) info->mod;
  }
  delete info;
}

int64_t Seek(AC_INFO* info, int64_t iSeekTime)
{
  //  Seek to the nearest sample
  // set the buffer size to 0 first, as this invokes a WriteCallback which
  // may be called when the buffer is almost full (resulting in a buffer
  // overrun unless we reset m_BufferSize first).
  if (! info || !info->mod)
    return -1;

  FLAC *mod = (FLAC *) info->mod;
  mod->buffersize = 0;
  FLAC__stream_decoder_seek_absolute(mod->decoder, (__int64)( iSeekTime * info->samplerate) / 1000);
    //CLog::Log(LOGERROR, "FLACCodec::Seek - failed to seek");

  if(FLAC__stream_decoder_get_state(mod->decoder) == FLAC__STREAM_DECODER_SEEK_ERROR)
  {
    //CLog::Log(LOGINFO, "FLACCodec::Seek - must reset decoder after seek");
    if(FLAC__stream_decoder_flush(mod->decoder))
      return -1;
  }

  return iSeekTime;
}

int ReadPCM(AC_INFO* info, void* pBuffer, unsigned int size, unsigned int *actualsize)
{
  *actualsize=0;
  if (!info || !info->mod)
    return READ_ERROR;

  FLAC* mod = (FLAC *) info->mod;

  bool eof = false;
  if (FLAC__stream_decoder_get_state(mod->decoder) == FLAC__STREAM_DECODER_END_OF_STREAM)
    eof = true;

  if (!eof)
  {
    //  fill our buffer 4 decoded frame (the buffer could hold 5)
    while(mod->buffersize < (mod->maxframesize * 4) &&
      FLAC__stream_decoder_get_state(mod->decoder) != FLAC__STREAM_DECODER_END_OF_STREAM)
    {
      if (!FLAC__stream_decoder_process_single(mod->decoder))
      {
        //CLog::Log(LOGERROR, "FLACCodec: Error decoding single block");
        return READ_ERROR;
      }
    }
  }

  if (size < mod->buffersize)
  { //  do we need less audio data then in our buffer
    memcpy(pBuffer, mod->buffer, size);
    memmove(mod->buffer, mod->buffer + size, mod->buffersize - size);
    mod->buffersize -= size;
    *actualsize = size;
  }
  else
  {
    memcpy(pBuffer, mod->buffer, mod->buffersize);
    *actualsize = mod->buffersize;
    mod->buffersize = 0;
  }

  if (eof && mod->buffer == 0)
    return READ_EOF;

  return READ_SUCCESS;
}

int GetNumberOfTracks(const char* strFile)
{
  AC_INFO* info = Init(strFile, 1);
  if (!info)
    return 0;
  
  unsigned int tracks = FLAC__stream_decoder_get_channels(((FLAC *) info->mod)->decoder);  
  DeInit(info);

  return tracks;
}

void FreeDecoder(AC_INFO* info)
{
  if (info && info->mod && ((FLAC *) info->mod)->decoder)
  {
    FLAC__stream_decoder_finish(((FLAC *) info->mod)->decoder);
    FLAC__stream_decoder_delete(((FLAC *) info->mod)->decoder);
    ((FLAC *) info->mod)->decoder = NULL;
  }
}

FLAC__StreamDecoderWriteStatus DecoderWriteCallback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
  AC_INFO* info = (AC_INFO*) client_data;
  if (!info || !info->mod)
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  FLAC *mod = (FLAC *) info->mod;

  const int bytes_per_sample = frame->header.bits_per_sample / 8;
  BYTE* outptr = mod->buffer + mod->buffersize;
  FLAC__int16* outptr16 = (FLAC__int16 *) outptr;
  FLAC__int32* outptr32 = (FLAC__int32 *) outptr;

  unsigned int current_sample = 0;
  for(current_sample = 0; current_sample < frame->header.blocksize; current_sample++)
  {
    for(unsigned int channel = 0; channel < frame->header.channels; channel++)
    {
      switch(bytes_per_sample)
      {
        case 2:
          outptr16[current_sample*frame->header.channels + channel] = (FLAC__int16) buffer[channel][current_sample];
          break;
        case 3:
          outptr[2] = (buffer[channel][current_sample] >> 16) & 0xff;
          outptr[1] = (buffer[channel][current_sample] >> 8 ) & 0xff;
          outptr[0] = (buffer[channel][current_sample] >> 0 ) & 0xff;
          outptr += bytes_per_sample;
          break;
        default:
          outptr32[current_sample*frame->header.channels + channel] = buffer[channel][current_sample];
          break;
      }
    }
  }

  if (bytes_per_sample == 1)
  {
    for(unsigned int i = 0; i < current_sample; i++)
    {
      BYTE* outptr = mod->buffer + mod->buffersize;
      outptr[i] ^= 0x80;
    }
  }

  mod->buffersize += current_sample * bytes_per_sample * frame->header.channels;

  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void DecoderMetadataCallback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
  AC_INFO* info = (AC_INFO*) client_data;
  if (!info)
    return;

  if (metadata->type==FLAC__METADATA_TYPE_STREAMINFO)
  {
    info->samplerate                   = metadata->data.stream_info.sample_rate;
    info->channels                     = metadata->data.stream_info.channels;
    info->bitpersample                 = metadata->data.stream_info.bits_per_sample;
    info->totaltime                    = (__int64)metadata->data.stream_info.total_samples * 1000 / metadata->data.stream_info.sample_rate;
    ((FLAC *) info->mod)->maxframesize = metadata->data.stream_info.max_blocksize *
      (info->bitpersample / 8) * info->channels;
  }
}

void DecoderErrorCallback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
  //CLog::Log(LOGERROR, "FLACCodec: Read error %i", status);
}

}