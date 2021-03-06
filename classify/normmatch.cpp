/******************************************************************************
 **	Filename:    normmatch.c
 **	Purpose:     Simple matcher based on character normalization features.
 **	Author:      Dan Johnson
 **	History:     Wed Dec 19 16:18:06 1990, DSJ, Created.
 **
 **	(c) Copyright Hewlett-Packard Company, 1988.
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 ** http://www.apache.org/licenses/LICENSE-2.0
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 ******************************************************************************/
/**----------------------------------------------------------------------------
          Include Files and Type Defines
----------------------------------------------------------------------------**/
#include "normmatch.h"
#include "clusttool.h"
#include "normfeat.h"
#include "debug.h"
#include "const.h"
#include "efio.h"
#include "emalloc.h"
#include "globals.h"
#include "scanutils.h"

#include <stdio.h>
#include <math.h>

/* define default filenames for training data */
#define NORM_PROTO_FILE   "tessdata/normproto"

typedef struct
{
  int NumParams;
  PARAM_DESC *ParamDesc;
  LIST Protos[MAX_CLASS_ID + 1];
}


NORM_PROTOS;

/**----------------------------------------------------------------------------
          Private Function Prototypes
----------------------------------------------------------------------------**/
FLOAT32 NormEvidenceOf(register FLOAT32 NormAdj);

void PrintNormMatch(FILE *File,
                    int NumParams,
                    PROTOTYPE *Proto,
                    FEATURE Feature);

NORM_PROTOS *ReadNormProtos(FILE *File);

/**----------------------------------------------------------------------------
        Global Data Definitions and Declarations
----------------------------------------------------------------------------**/
/* global data structure to hold char normalization protos */
static NORM_PROTOS *NormProtos;

/* name of file containing char normalization protos */
static const char *NormProtoFile = NORM_PROTO_FILE;

/* control knobs used to control the normalization adjustment process */
make_float_var (NormAdjMidpoint, 32.0, MakeNormAdjMidpoint,
15, 16, SetNormAdjMidpoint, "Norm adjust midpoint ...")
make_float_var (NormAdjCurl, 2.0, MakeNormAdjCurl,
15, 17, SetNormAdjCurl, "Norm adjust curl ...")
//extern char *demodir;
/**----------------------------------------------------------------------------
              Public Code
----------------------------------------------------------------------------**/
/*---------------------------------------------------------------------------*/
FLOAT32 ComputeNormMatch(CLASS_ID ClassId, FEATURE Feature, BOOL8 DebugMatch) {
/*
 **	Parameters:
 **		ClassId		id of class to match against
 **		Feature		character normalization feature
 **		DebugMatch	controls dump of debug info
 **	Globals:
 **		NormProtos	character normalization prototypes
 **	Operation: This routine compares Features against each character
 **		normalization proto for ClassId and returns the match
 **		rating of the best match.
 **	Return: Best match rating for Feature against protos of ClassId.
 **	Exceptions: none
 **	History: Wed Dec 19 16:56:12 1990, DSJ, Created.
 */
  LIST Protos;
  FLOAT32 BestMatch;
  FLOAT32 Match;
  FLOAT32 Delta;
  PROTOTYPE *Proto;
  int ProtoId;

  /* handle requests for classification as noise */
  if (ClassId == NO_CLASS) {
    /* kludge - clean up constants and make into control knobs later */
    Match = (ParamOf (Feature, CharNormLength) *
      ParamOf (Feature, CharNormLength) * 500.0 +
      ParamOf (Feature, CharNormRx) *
      ParamOf (Feature, CharNormRx) * 8000.0 +
      ParamOf (Feature, CharNormRy) *
      ParamOf (Feature, CharNormRy) * 8000.0);
    return (1.0 - NormEvidenceOf (Match));
  }

  BestMatch = MAX_FLOAT32;
  Protos = NormProtos->Protos[ClassId];

  if (DebugMatch) {
    cprintf ("\nFeature = ");
    WriteFeature(stdout, Feature);
  }

  ProtoId = 0;
  iterate(Protos) {
    Proto = (PROTOTYPE *) first (Protos);
    Delta = ParamOf (Feature, CharNormY) - Proto->Mean[CharNormY];
    Match = Delta * Delta * Proto->Weight.Elliptical[CharNormY];
    Delta = ParamOf (Feature, CharNormRx) - Proto->Mean[CharNormRx];
    Match += Delta * Delta * Proto->Weight.Elliptical[CharNormRx];

    if (Match < BestMatch)
      BestMatch = Match;

    if (DebugMatch) {
      cprintf ("Proto %1d = ", ProtoId);
      WriteNFloats (stdout, NormProtos->NumParams, Proto->Mean);
      cprintf ("      var = ");
      WriteNFloats (stdout, NormProtos->NumParams,
        Proto->Variance.Elliptical);
      cprintf ("    match = ");
      PrintNormMatch (stdout, NormProtos->NumParams, Proto, Feature);
    }
    ProtoId++;
  }
  return (1.0 - NormEvidenceOf (BestMatch));
}                                /* ComputeNormMatch */


/*---------------------------------------------------------------------------*/
void GetNormProtos() {
/*
 **	Parameters: none
 **	Globals:
 **		NormProtoFile	name of file containing normalization protos
 **		NormProtos	global data structure to hold protos
 **	Operation: This routine reads in a set of character normalization
 **		protos from NormProtoFile and places them into NormProtos.
 **	Return: none
 **	Exceptions: none
 **	History: Wed Dec 19 16:24:25 1990, DSJ, Created.
 */
  FILE *File;
  char name[1024];

  strcpy(name, demodir);
  strcat(name, NormProtoFile);
  File = Efopen (name, "r");
  NormProtos = ReadNormProtos (File);
  fclose(File);

}                                /* GetNormProtos */

void FreeNormProtos() {
  if (NormProtos != NULL) {
    for (int i = 0; i <= MAX_CLASS_ID; i++)
      FreeProtoList(&NormProtos->Protos[i]);
    Efree(NormProtos->ParamDesc);
    Efree(NormProtos);
    NormProtos = NULL;
  }
}

/*---------------------------------------------------------------------------*/
void InitNormProtoVars() {
/*
 **	Parameters: none
 **	Globals:
 **		NormProtoFile		filename for normalization protos
 **	Operation: Initialize the control variables for the normalization
 **				matcher.
 **	Return: none
 **	Exceptions: none
 **	History: Mon Nov  5 17:22:10 1990, DSJ, Created.
 */
  VALUE dummy;

  string_variable (NormProtoFile, "NormProtoFile", NORM_PROTO_FILE);

  MakeNormAdjMidpoint();
  MakeNormAdjCurl();

}                                /* InitNormProtoVars */


/**----------------------------------------------------------------------------
              Private Code
----------------------------------------------------------------------------**/
/**********************************************************************
 * NormEvidenceOf
 *
 * Return the new type of evidence number corresponding to this
 * normalization adjustment.  The equation that represents the transform is:
 *       1 / (1 + (NormAdj / midpoint) ^ curl)
 **********************************************************************/
FLOAT32 NormEvidenceOf(register FLOAT32 NormAdj) {
  NormAdj /= NormAdjMidpoint;

  if (NormAdjCurl == 3)
    NormAdj = NormAdj * NormAdj * NormAdj;
  else if (NormAdjCurl == 2)
    NormAdj = NormAdj * NormAdj;
  else
    NormAdj = pow (NormAdj, NormAdjCurl);
  return (1.0 / (1.0 + NormAdj));
}


/*---------------------------------------------------------------------------*/
void PrintNormMatch(FILE *File,
                    int NumParams,
                    PROTOTYPE *Proto,
                    FEATURE Feature) {
/*
 **	Parameters:
 **		File		open text file to dump match debug info to
 **		NumParams	# of parameters in proto and feature
 **		Proto[]		array of prototype parameters
 **		Feature[]	array of feature parameters
 **	Globals: none
 **	Operation: This routine dumps out detailed normalization match info.
 **	Return: none
 **	Exceptions: none
 **	History: Wed Jan  2 09:49:35 1991, DSJ, Created.
 */
  int i;
  FLOAT32 ParamMatch;
  FLOAT32 TotalMatch;

  for (i = 0, TotalMatch = 0.0; i < NumParams; i++) {
    ParamMatch = ((ParamOf (Feature, i) - Mean (Proto, i)) /
      StandardDeviation (Proto, i));

    fprintf (File, " %6.1f", ParamMatch);

    if (i == CharNormY || i == CharNormRx)
      TotalMatch += ParamMatch * ParamMatch;
  }
  fprintf (File, " --> %6.1f (%4.2f)\n",
    TotalMatch, NormEvidenceOf (TotalMatch));

}                                /* PrintNormMatch */


/*---------------------------------------------------------------------------*/
NORM_PROTOS *ReadNormProtos(FILE *File) {
/*
 **	Parameters:
 **		File	open text file to read normalization protos from
 **	Globals: none
 **	Operation: This routine allocates a new data structure to hold
 **		a set of character normalization protos.  It then fills in
 **		the data structure by reading from the specified File.
 **	Return: Character normalization protos.
 **	Exceptions: none
 **	History: Wed Dec 19 16:38:49 1990, DSJ, Created.
 */
  NORM_PROTOS *NormProtos;
  int i;
  char ClassId[2];
  LIST Protos;
  int NumProtos;

  /* allocate and initialization data structure */
  NormProtos = (NORM_PROTOS *) Emalloc (sizeof (NORM_PROTOS));
  for (i = 0; i <= MAX_CLASS_ID; i++)
    NormProtos->Protos[i] = NIL;

  /* read file header and save in data structure */
  NormProtos->NumParams = ReadSampleSize (File);
  NormProtos->ParamDesc = ReadParamDesc (File, NormProtos->NumParams);

  /* read protos for each class into a separate list */
  while (fscanf (File, "%1s %d", ClassId, &NumProtos) == 2) {
    Protos = NormProtos->Protos[ClassId[0]];
    for (i = 0; i < NumProtos; i++)
      Protos =
        push_last (Protos, ReadPrototype (File, NormProtos->NumParams));
    NormProtos->Protos[ClassId[0]] = Protos;
  }

  return (NormProtos);

}                                /* ReadNormProtos */
