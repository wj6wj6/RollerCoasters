/************************************************************************
     File:        Track.cpp

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     Container for the "World"

						This provides a container for all of the "stuff" 
						in the world.

						It could have been all global variables, or it could 
						have just been
						contained in the window. The advantage of doing it 
						this way is that
						we might have multiple windows looking at the same 
						world. But, I don't	think we'll actually do that.

						See the readme for commentary on code style

     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#include "Track.H"

#include <FL/fl_ask.h>
#include <math.h>

//****************************************************************************
//
// * Constructor
//============================================================================
CTrack::
CTrack() : trainU(0)
//============================================================================
{
	resetPoints();
}

//****************************************************************************
//
// * provide a default set of points
//============================================================================
void CTrack::
resetPoints()
//============================================================================
{

	points.clear();
	points.push_back(ControlPoint(Pnt3f(50,5,0)));
	points.push_back(ControlPoint(Pnt3f(0,5,50)));
	points.push_back(ControlPoint(Pnt3f(-50,5,0)));
	points.push_back(ControlPoint(Pnt3f(0,5,-50)));

	// we had better put the train back at the start of the track...
	trainU = 0.0;
}

//****************************************************************************
//
// * Evaluate one segment of the selected closed spline
//============================================================================
static float evaluateSplineComponent(float p0, float p1, float p2, float p3,
									 float t, TrackSplineType splineType)
//============================================================================
{
	const float t2 = t * t;
	const float t3 = t2 * t;

	switch (splineType) {
	case TRACK_SPLINE_LINEAR:
		return p1 + (p2 - p1) * t;
	case TRACK_SPLINE_CARDINAL:
		return 0.5f * (2.0f * p1
			+ (-p0 + p2) * t
			+ (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
			+ (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	case TRACK_SPLINE_BSPLINE:
	default:
		return ((1.0f - 3.0f * t + 3.0f * t2 - t3) * p0
			+ (4.0f - 6.0f * t2 + 3.0f * t3) * p1
			+ (1.0f + 3.0f * t + 3.0f * t2 - 3.0f * t3) * p2
			+ t3 * p3) / 6.0f;
	}
}

Pnt3f CTrack::
evaluatePosition(float u, TrackSplineType splineType) const
//============================================================================
{
	const int pointCount = static_cast<int>(points.size());
	if (pointCount < 4)
		return Pnt3f();

	// Wrap the parameter so negative and overflowing values stay on the loop.
	float wrappedU = u - floorf(u / pointCount) * pointCount;
	int segment = static_cast<int>(wrappedU);
	float t = wrappedU - segment;
	int indices[4];
	for (int i = 0; i < 4; ++i) {
		indices[i] = (segment + i - 1) % pointCount;
		if (indices[i] < 0)
			indices[i] += pointCount;
	}

	const Pnt3f& p0 = points[indices[0]].pos;
	const Pnt3f& p1 = points[indices[1]].pos;
	const Pnt3f& p2 = points[indices[2]].pos;
	const Pnt3f& p3 = points[indices[3]].pos;
	Pnt3f result;
	result.x = evaluateSplineComponent(p0.x, p1.x, p2.x, p3.x, t, splineType);
	result.y = evaluateSplineComponent(p0.y, p1.y, p2.y, p3.y, t, splineType);
	result.z = evaluateSplineComponent(p0.z, p1.z, p2.z, p3.z, t, splineType);

	return result;
}

//****************************************************************************
//
// * Estimate the tangent with a centered finite difference
//============================================================================
Pnt3f CTrack::
evaluateTangent(float u, TrackSplineType splineType) const
//============================================================================
{
	const float sampleStep = 0.001f;
	const Pnt3f before = evaluatePosition(u - sampleStep, splineType);
	const Pnt3f after = evaluatePosition(u + sampleStep, splineType);
	Pnt3f tangent(after.x - before.x, after.y - before.y, after.z - before.z);
	tangent.normalize();
	return tangent;
}

//****************************************************************************
//
// * Handy utility to break a string into a list of words
//============================================================================
void breakString(char* str, std::vector<const char*>& words) 
//============================================================================
{
	// start with no words
	words.clear();

	// scan through the string, starting at the beginning
	char* p = str;

	// stop when we hit the end of the string
	while(*p) {
		// skip over leading whitespace - stop at the first character or end of string
		while (*p && *p<=' ') p++;

		// now we're pointing at the first thing after the spaces
		// make sure its not a comment, and that we're not at the end of the string
		// (that's actually the same thing)
		if (! (*p) || *p == '#')
		break;

		// so we're pointing at a word! add it to the word list
		words.push_back(p);

		// now find the end of the word
		while(*p > ' ') p++;	// stop at space or end of string

		// if its ethe end of the string, we're done
		if (! *p) break;

		// otherwise, turn this space into and end of string (to end the word)
		// and keep going
		*p = 0;
		p++;
	}
}

//****************************************************************************
//
// * The file format is simple
//   first line: an integer with the number of control points
//	  other lines: one line per control point
//   either 3 (X,Y,Z) numbers on the line, or 6 numbers (X,Y,Z, orientation)
//============================================================================
void CTrack::
readPoints(const char* filename)
//============================================================================
{
	FILE* fp = fopen(filename,"r");
	if (!fp) {
		fl_alert("Can't Open File!\n");
	} 
	else {
		char buf[512];

		// first line = number of points
		fgets(buf,512,fp);
		size_t npts = (size_t) atoi(buf);

		if( (npts<4) || (npts>65535)) {
			fl_alert("Illegal Number of Points Specified in File");
		} else {
			points.clear();
			// get lines until EOF or we have enough points
			while( (points.size() < npts) && fgets(buf,512,fp) ) {
				Pnt3f pos,orient;
				vector<const char*> words;
				breakString(buf,words);
				if (words.size() >= 3) {
					pos.x = (float) strtod(words[0],0);
					pos.y = (float) strtod(words[1],0);
					pos.z = (float) strtod(words[2],0);
				} else {
					pos.x=0;
					pos.y=0;
					pos.z=0;
				}
				if (words.size() >= 6) {
					orient.x = (float) strtod(words[3],0);
					orient.y = (float) strtod(words[4],0);
					orient.z = (float) strtod(words[5],0);
				} else {
					orient.x = 0;
					orient.y = 1;
					orient.z = 0;
				}
				orient.normalize();
				points.push_back(ControlPoint(pos,orient));
			}
		}
		fclose(fp);
	}
	trainU = 0;
}

//****************************************************************************
//
// * write the control points to our simple format
//============================================================================
void CTrack::
writePoints(const char* filename)
//============================================================================
{
	FILE* fp = fopen(filename,"w");
	if (!fp) {
		fl_alert("Can't open file for writing");
	} else {
		fprintf(fp,"%d\n",points.size());
		for(size_t i=0; i<points.size(); ++i)
			fprintf(fp,"%g %g %g %g %g %g\n",
				points[i].pos.x, points[i].pos.y, points[i].pos.z, 
				points[i].orient.x, points[i].orient.y, points[i].orient.z);
		fclose(fp);
	}
}
