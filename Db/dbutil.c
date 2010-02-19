/*
* $Id:  $
* $Version: $
*
* Copyright (c) Priit J�rv 2010
*
* Minor mods by Tanel Tammet
*
* This file is part of wgandalf
*
* Wgandalf is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* Wgandalf is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with Wgandalf.  If not, see <http://www.gnu.org/licenses/>.
*
*/

 /** @file dbutil.c
 * Miscellaneous utility functions.
 */

/* ====== Includes =============== */

#include <stdio.h>
#include <stdlib.h>
#include "dbdata.h"

/* ====== Private headers and defs ======== */

#include "dbutil.h"

#ifdef _WIN32
#define snprintf(s, sz, f, ...) _snprintf_s(s, sz+1, sz, f, ## __VA_ARGS__)
#endif

#ifdef _WIN32
#define sscanf sscanf_s  /* XXX: This will break for string parameters */
#endif

#define CSV_FIELD_BUF 4096      /** max size of csv I/O field */
#define CSV_FIELD_SEPARATOR ';' /** field separator, comma or semicolon */
#define CSV_ENCDATA_BUF 10      /** initial storage for encoded (gint) data */


/* ======= Private protos ================ */

static gint show_io_error(void *db, char *errmsg);
static gint show_io_error_str(void *db, char *errmsg, char *str);
static void snprint_value_csv(void *db, gint enc, char *buf, int buflen);
static gint fread_csv(void *db, FILE *f);


/* ====== Functions ============== */

/** Print contents of database.
 * 
 */

void wg_print_db(void *db) {
  void *rec;
  
  rec = wg_get_first_record(db);
  do{    
    wg_print_record(db,rec);
    printf("\n");   
    rec = wg_get_next_record(db,rec);    
  } while(rec);
}

/** Print single record
 *
 */
void wg_print_record(void *db, wg_int* rec) {

  wg_int len, enc;
  int i;
  char strbuf[256];
  
  if (rec==NULL) {
    printf("<null rec pointer>\n");
    return;
  }  
  len = wg_get_record_len(db, rec);
  printf("[");
  for(i=0; i<len; i++) {
    if(i) printf(",");
    enc = wg_get_field(db, rec, i);
    wg_snprint_value(db, enc, strbuf, 255);
    printf(strbuf);
  }
  printf("]");
}

/** Print a single, encoded value
 *  The value is written into a character buffer.
 */
void wg_snprint_value(void *db, gint enc, char *buf, int buflen) {
  int intdata;
  char *strdata;
  double doubledata;
  char strbuf[80];

  buflen--; /* snprintf adds '\0' */
  switch(wg_get_encoded_type(db, enc)) {
    case WG_NULLTYPE:
      snprintf(buf, buflen, "NULL");
      break;
    case WG_RECORDTYPE:
      intdata = (int) wg_decode_record(db, enc);
      snprintf(buf, buflen, "<record at %x>", intdata);
      wg_print_record(db,(wg_int*)intdata);
      break;
    case WG_INTTYPE:
      intdata = wg_decode_int(db, enc);
      snprintf(buf, buflen, "%d", intdata);
      break;
    case WG_DOUBLETYPE:
      doubledata = wg_decode_double(db, enc);
      snprintf(buf, buflen, "%f", doubledata);
      break;
    case WG_STRTYPE:
      strdata = wg_decode_str(db, enc);
      snprintf(buf, buflen, "\"%s\"", strdata);
      break;
    case WG_CHARTYPE:
      intdata = wg_decode_char(db, enc);
      snprintf(buf, buflen, "%c", (char) intdata);
      break;
    case WG_DATETYPE:
      intdata = wg_decode_date(db, enc);
      wg_strf_iso_datetime(db,intdata,0,strbuf);
      strbuf[10]=0;
      snprintf(buf, buflen, "<raw date %d>%s", intdata,strbuf);
      break;
    case WG_TIMETYPE:
      intdata = wg_decode_time(db, enc);
      wg_strf_iso_datetime(db,1,intdata,strbuf);        
      snprintf(buf, buflen, "<raw time %d>%s",intdata,strbuf+11);
      break;
    default:
      snprintf(buf, buflen, "<unsupported type>");
      break;
  }
}

/** Print a single, encoded value, into a CSV-friendly format
 *  The value is written into a character buffer.
 */
static void snprint_value_csv(void *db, gint enc, char *buf, int buflen) {
  int intdata;
  double doubledata;
  char strbuf[80], *iptr, *optr;

  buflen--; /* snprintf adds '\0' */
  switch(wg_get_encoded_type(db, enc)) {
    case WG_NULLTYPE:
      buf[0] = '\0'; /* output an empty field */
      break;
    case WG_RECORDTYPE:
      intdata = ptrtooffset(db, wg_decode_record(db, enc));
      snprintf(buf, buflen, "\"<record offset %d>\"", intdata);
      break;
    case WG_INTTYPE:
      intdata = wg_decode_int(db, enc);
      snprintf(buf, buflen, "%d", intdata);
      break;
    case WG_DOUBLETYPE:
      doubledata = wg_decode_double(db, enc);
      snprintf(buf, buflen, "%f", doubledata);
      break;
    case WG_STRTYPE:
#ifdef CHECK
      if(buflen < 3) {
        show_io_error(db, "CSV field buffer too small");
        return;
      }
#endif
      iptr = wg_decode_str(db, enc);
      optr = buf;
      *optr++ = '"';
      buflen--; /* space for terminating quote */
      while(*iptr) { /* \0 terminates */
        int nextsz = 1;      
        if(*iptr == '"') nextsz++;

        /* Will our string fit? */
        if(((int)optr + nextsz - (int)buf) < buflen) {
          *optr++ = *iptr;
          if(*iptr++ == '"')
            *optr++ = '"'; /* quote -> double quote */
        } else
          break;
      }
      *optr++ = '"'; /* CSV string terminator */
      *optr = '\0'; /* C string terminator */
      break;
    case WG_CHARTYPE:
      intdata = wg_decode_char(db, enc);
      snprintf(buf, buflen, "%c", (char) intdata);
      break;
    case WG_DATETYPE:
      intdata = wg_decode_date(db, enc);
      wg_strf_iso_datetime(db,intdata,0,strbuf);
      strbuf[10]=0;
      snprintf(buf, buflen, "%s", strbuf);
      break;
    case WG_TIMETYPE:
      intdata = wg_decode_time(db, enc);
      wg_strf_iso_datetime(db,1,intdata,strbuf);        
      snprintf(buf, buflen, "%s", strbuf+11);
      break;
    default:
      snprintf(buf, buflen, "\"<unsupported type>\"");
      break;
  }
}

/** Parse value from string, encode it for Wgandalf
 *  returns WG_ILLEGAL if value could not be parsed or
 *  encoded.
 *  XXX: currently limited support for data types (str and int only).
 */
gint wg_parse_and_encode(void *db, char *buf) {
  int intdata;
  gint encoded = WG_ILLEGAL;

  if(buf[0] == 0) {
    encoded = 0; /* empty fields become NULL-s */
  } else if(sscanf(buf, "%d", &intdata)) {
    encoded = wg_encode_int(db, intdata);
  } else {
    encoded = wg_encode_str(db, buf, NULL);
  }
  return encoded;
}

/** Write single record to stream in CSV format
 *
 */
void wg_fprint_record_csv(void *db, wg_int* rec, FILE *f) {

  wg_int len, enc;
  int i;
  char *strbuf;
  
  if(rec==NULL) {
    show_io_error(db, "null record pointer");
    return;
  }  

  strbuf = (char *) malloc(CSV_FIELD_BUF);
  if(strbuf==NULL) {
    show_io_error(db, "Failed to allocate memory");
    return;
  }  

  len = wg_get_record_len(db, rec);
  for(i=0; i<len; i++) {
    if(i) fprintf(f, "%c", CSV_FIELD_SEPARATOR);
    enc = wg_get_field(db, rec, i);
    snprint_value_csv(db, enc, strbuf, CSV_FIELD_BUF-1);
    fprintf(f, strbuf);
  }

  free(strbuf);
}

/** Export contents of database into a CSV file.
 * 
 */

void wg_export_db_csv(void *db, char *filename) {
  void *rec;
  FILE *f;
  
#ifdef _WIN32
  if(fopen_s(&f, filename, "w")) {
#else
  if(!(f = fopen(filename, "w"))) {
#endif
    show_io_error_str(db, "failed to open file", filename);
    return;
  }

  rec = wg_get_first_record(db);
  while(rec) {
    wg_fprint_record_csv(db, rec, f);
    fprintf(f, "\n");
    rec = wg_get_next_record(db, rec);
  };

  fclose(f);
}

/** Read CSV stream and convert it to database records.
 *  Returns 0 if there were no errors
 *  Returns -1 for non-fatal errors
 *  Returns -2 for database errors
 *  Returns -3 for other errors
 */
static gint fread_csv(void *db, FILE *f) {
  char *strbuf, *ptr;
  gint *encdata;
  gint err = 0;
  gint uq_field, quoted_field, esc_quote, eat_sep; /** State flags */
  gint commit_strbuf, commit_record;
  gint reclen;
  gint encdata_sz = CSV_ENCDATA_BUF;

  strbuf = (char *) malloc(CSV_FIELD_BUF);
  if(strbuf==NULL) {
    show_io_error(db, "Failed to allocate memory");
    return -1;
  }

  encdata = (gint *) malloc(sizeof(gint) * encdata_sz);
  if(strbuf==NULL) {
    free(strbuf);
    show_io_error(db, "Failed to allocate memory");
    return -1;
  }

  /* Init parser state */
  reclen = 0;
  uq_field = quoted_field = esc_quote = eat_sep = 0;
  commit_strbuf = commit_record = 0;
  ptr = strbuf;

  while(!feof(f)) {
    /* Parse cycle consists:
     * 1. read character from stream. This can either:
     *   - change the state of the parser
     *   - be appended to strbuf
     * 2. if the parser state changed, we need to do one
     *    of the following:
     *   - parse the field from strbuf
     *   - store the record in the database
     */
      
    char c = (char) fgetc(f);

    if(quoted_field) {
      /* We're parsing a quoted field. Anything we get is added to
       * strbuf unless it's a quote character.
       */
      if(!esc_quote && c == '"') {
        char nextc = (char) fgetc(f);
        ungetc((int) nextc, f);

        if(nextc!='"') {
          /* Field ends. Note that even EOF is acceptable here */
          quoted_field = 0;
          commit_strbuf = 1; /* set flag to commit buffer */
          eat_sep = 1; /* next separator can be ignored. */
        } else {
          esc_quote = 1; /* make a note that next quote is escaped */
        }
      } else {
        esc_quote = 0;
        /* read the character. It's simply ignored if the buffer is full */
        if(((int) ptr - (int) strbuf) < CSV_FIELD_BUF-1)
          *ptr++ = c;
      }
    } else if(uq_field) {
      /* In case of an unquoted field, terminator can be the field
       * separator or end of line. In the latter case we also need to
       * store the record.
       */
      if(c == CSV_FIELD_SEPARATOR) {
        uq_field = 0;
        commit_strbuf = 1;
      } else if(c == 13) { /* Ignore CR. */
        continue;
      } else if(c == 10) { /* LF is the last character for both DOS and UNIX */
        uq_field = 0;
        commit_strbuf = 1;
        commit_record = 1;
      } else {
        if(((int) ptr - (int) strbuf) < CSV_FIELD_BUF-1)
          *ptr++ = c;
      }
    } else {
      /* We are currently not parsing anything. Four things can happen:
       * - quoted field begins
       * - unquoted field begins
       * - we're on a field separator
       * - line ends
       */
      if(c == CSV_FIELD_SEPARATOR) {
        if(eat_sep) {
          /* A quoted field just ended, this separator can be skipped */
          eat_sep = 0;
          continue;
        } else {
          /* The other way to interpret this is that we have a NULL field.
           * Commit empty buffer. */
          commit_strbuf = 1;
        }
      } else if(c == 13) { /* CR is ignored, as we're expecting LF to follow */
        continue;
      } else if(c == 10) { /* LF is line terminator. */
        if(eat_sep) {
          eat_sep = 0; /* should reset this as well */
        } else if(reclen) {
          /* This state can occur when we've been preceded by a record
           * separator. The zero length field between ,\n counts as a NULL
           * field. XXX: this creates an inconsistent situation where
           * empty lines are discarded while a sincle comma (or semicolon)
           * generates a two-field record of NULL-s. The benefit is that
           * junk empty lines don't generate junk records. Check the
           * unofficial RFC to see if this should be changed.
           */
          commit_strbuf = 1;
        }
        commit_record = 1;
      } else {
        /* A new field begins */
        if(c == '"') {
          quoted_field = 1;
        }
        else {
          uq_field = 1;
          *ptr++ = c;
        }
      }
    }
  
    if(commit_strbuf) {
      gint enc;

      /* We were instructed to convert our string buffer to data. First
       * mark the end of string and reset strbuf state for next loop. */
      *ptr = (char) 0;
      commit_strbuf = 0;
      ptr = strbuf;

      /* Need more storage for encoded data? */
      if(reclen >= encdata_sz) {
        gint *tmp;
        encdata_sz += CSV_ENCDATA_BUF;
        tmp = (gint *) realloc(encdata, sizeof(gint) * encdata_sz);
        if(tmp==NULL) {
          err = -3;
          show_io_error(db, "Failed to allocate memory");
          break;
        } else
          encdata = tmp;
      }

      /* Do the actual parsing. This also allocates database-side
       * storage for the new data. */
      enc = wg_parse_and_encode(db, strbuf);
      if(enc == WG_ILLEGAL) {
        show_io_error_str(db, "Warning: failed to parse", strbuf);
        enc = 0; /* continue anyway */
      }
      encdata[reclen++] = enc;
    }

    if(commit_record) {
      /* Need to save the record to database. */
      int i;
      void *rec;

      commit_record = 0;
      if(!reclen)
        continue; /* Ignore empty rows */

      rec = wg_create_record(db, reclen);
      if(!rec) {
        err = -2;
        show_io_error(db, "Failed to create record");
        break;
      }
      for(i=0; i<reclen; i++) {
        if(wg_set_field(db, rec, i, encdata[i])) {
          err = -2;
          show_io_error(db, "Failed to save field data");
          break;
        }
      }
      
      /* Reset record data */
      reclen = 0;
    }
  }

  free(encdata);
  free(strbuf);
  return err;
}

/** Import data from a CSV file into database
 *  Data will be added to existing data.
 *  Returns 0 if there were no errors
 *  Returns -1 for file I/O errors
 *  Other error codes may be generated by fread_csv()
 */

gint wg_import_db_csv(void *db, char *filename) {
  FILE *f;
  gint err = 0;
  
#ifdef _WIN32
  if(fopen_s(&f, filename, "r")) {
#else
  if(!(f = fopen(filename, "r"))) {
#endif
    show_io_error_str(db, "failed to open file", filename);
    return -1;
  }

  err = fread_csv(db, f);
  fclose(f);
  return err;
}

/* ------------ error handling ---------------- */

static gint show_io_error(void *db, char *errmsg) {
  fprintf(stderr,"wgandalf I/O error: %s.\n", errmsg);
  return -1;
}

static gint show_io_error_str(void *db, char *errmsg, char *str) {
  fprintf(stderr,"wgandalf I/O error: %s: %s.\n", errmsg, str);
  return -1;
}