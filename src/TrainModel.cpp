/************************************************************************
     File:        TrainModel.cpp

     Comment:     See TrainModel.h. Every car is modelled facing +z with
                  the top of the rails at y = 0.
*************************************************************************/
#include "TrainModel.h"

#include "TrackRenderer.H"

#include <glad/glad.h>
#include <math.h>

namespace
{
const int kCars = 3;
const float kCarSpacing = 6.4f; // along the track, centre to centre

bool gShadowPass = false;

void colour(int r, int g, int b)
{
    if (!gShadowPass)
        glColor3ub(static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b));
}

// What the lighting shader reads from texture unit 1: how much the surface
// glows by itself (the rest: no sway, no crystal light, full sky light).
void glow(float amount)
{
    glMultiTexCoord4f(GL_TEXTURE1, amount, 0.0f, 0.0f, 1.0f);
}

// A box from its two opposite corners.
void box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glVertex3f(x0, y0, z1);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x0, y1, z1);
    glNormal3f(0, 0, -1);
    glVertex3f(x1, y0, z0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x0, y1, z0);
    glVertex3f(x1, y1, z0);
    glNormal3f(0, 1, 0);
    glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0);
    glNormal3f(0, -1, 0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x0, y0, z1);
    glNormal3f(1, 0, 0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1);
    glNormal3f(-1, 0, 0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x0, y0, z1);
    glVertex3f(x0, y1, z1);
    glVertex3f(x0, y1, z0);
    glEnd();
}

// The same box on both sides of the car.
void boxPair(float x0, float y0, float z0, float x1, float y1, float z1)
{
    box(x0, y0, z0, x1, y1, z1);
    box(-x1, y0, z0, -x0, y1, z1);
}

// A rider: legs, body, a square head with hair, and both arms up, waving.
void drawRider(float x, float z, int who, float time)
{
    const int shirts[4][3] = {{214, 60, 60}, {250, 196, 50}, {70, 170, 90}, {240, 240, 240}};
    const int hairs[4][3] = {{60, 40, 28}, {30, 30, 34}, {190, 140, 60}, {120, 60, 30}};
    const int *shirt = shirts[who % 4];
    const int *hair = hairs[(who / 2 + who) % 4];

    colour(50, 60, 110);
    box(x - 0.3f, 1.3f, z - 0.05f, x + 0.3f, 1.5f, z + 0.6f); // legs along the seat
    colour(shirt[0], shirt[1], shirt[2]);
    box(x - 0.32f, 1.5f, z - 0.2f, x + 0.32f, 2.2f, z + 0.15f);
    colour(236, 188, 150);
    box(x - 0.25f, 2.2f, z - 0.27f, x + 0.25f, 2.7f, z + 0.23f);
    colour(hair[0], hair[1], hair[2]);
    box(x - 0.27f, 2.62f, z - 0.3f, x + 0.27f, 2.8f, z + 0.25f);
    box(x - 0.27f, 2.3f, z - 0.32f, x + 0.27f, 2.7f, z - 0.25f);
    // eyes, so the face reads from the front
    colour(40, 40, 50);
    box(x - 0.17f, 2.42f, z + 0.23f, x - 0.07f, 2.52f, z + 0.245f);
    box(x + 0.07f, 2.42f, z + 0.23f, x + 0.17f, 2.52f, z + 0.245f);

    for (int side = -1; side <= 1; side += 2)
    {
        const float s = static_cast<float>(side);
        const float wave = sinf(time * 6.0f + static_cast<float>(who) * 1.7f + s) * 14.0f;
        glPushMatrix();
        glTranslatef(x + 0.4f * s, 2.1f, z);
        glRotatef(-18.0f * s + wave, 0.0f, 0.0f, 1.0f);
        colour(shirt[0], shirt[1], shirt[2]);
        box(-0.09f, 0.0f, -0.1f, 0.09f, 0.35f, 0.1f);
        colour(236, 188, 150);
        box(-0.08f, 0.35f, -0.09f, 0.08f, 0.8f, 0.09f);
        glPopMatrix();
    }
}

// One car. `first` gets the pointed nose and head lamps, `last` the tail lamps.
void drawCar(int index, bool first, bool last, float time)
{
    // running gear: axles, wheels with bright hubs, guide wheels under the rail
    colour(38, 40, 46);
    box(-1.1f, 0.38f, -2.0f, 1.1f, 0.62f, 2.0f); // chassis
    box(-1.45f, 0.3f, 1.3f, 1.45f, 0.44f, 1.5f); // axles
    box(-1.45f, 0.3f, -1.5f, 1.45f, 0.44f, -1.3f);
    for (int end = -1; end <= 1; end += 2)
    {
        const float z = 1.4f * static_cast<float>(end);
        colour(24, 24, 28);
        boxPair(1.05f, 0.02f, z - 0.36f, 1.4f, 0.72f, z + 0.36f);
        colour(196, 200, 208);
        boxPair(1.4f, 0.24f, z - 0.13f, 1.46f, 0.5f, z + 0.13f);
        colour(230, 150, 40);
        boxPair(1.16f, -0.26f, z - 0.16f, 1.34f, 0.0f, z + 0.16f);
    }

    // the tub: floor, sides with a gold stripe, front and back
    const int body[3][3] = {{24, 118, 172}, {28, 150, 160}, {40, 96, 180}};
    const int *paint = body[index % 3];
    colour(paint[0], paint[1], paint[2]);
    box(-1.5f, 0.62f, -2.5f, 1.5f, 0.86f, 2.5f);
    boxPair(1.32f, 0.86f, -2.5f, 1.5f, 1.72f, 2.5f);
    box(-1.5f, 0.86f, -2.5f, 1.5f, 2.0f, -2.3f);
    box(-1.5f, 0.86f, 2.3f, 1.5f, 1.5f, 2.5f);
    colour(246, 204, 70);
    boxPair(1.5f, 1.3f, -2.5f, 1.54f, 1.46f, 2.5f);
    box(-1.5f, 2.0f, -2.52f, 1.5f, 2.1f, -2.28f); // trim on the back
    colour(paint[0] + 40, paint[1] + 40, paint[2] + 40);
    boxPair(1.32f, 1.72f, -2.5f, 1.5f, 1.8f, 2.5f); // lighter rim

    // two rows of seats with head rests, and a yellow lap bar for each
    for (int row = 0; row < 2; ++row)
    {
        const float z = row == 0 ? 0.55f : -1.55f;
        colour(46, 48, 58);
        box(-1.25f, 0.86f, z - 0.25f, 1.25f, 1.3f, z + 0.75f);
        box(-1.25f, 1.3f, z - 0.45f, 1.25f, 2.25f, z - 0.2f);
        colour(70, 74, 90);
        boxPair(0.3f, 2.25f, z - 0.45f, 1.0f, 2.6f, z - 0.2f);
        colour(250, 208, 40);
        box(-1.3f, 1.62f, z + 0.5f, 1.3f, 1.74f, z + 0.62f);
        boxPair(1.2f, 0.9f, z + 0.5f, 1.3f, 1.74f, z + 0.62f);
        drawRider(-0.65f, z, index * 4 + row * 2, time);
        drawRider(0.65f, z, index * 4 + row * 2 + 1, time);
    }

    // coupling to the car behind
    if (!last)
    {
        colour(60, 62, 70);
        box(-0.14f, 0.44f, -3.3f, 0.14f, 0.62f, -2.5f);
    }

    if (first)
    {
        // a stepped, pointed nose with a gold crest and two head lamps
        colour(paint[0], paint[1], paint[2]);
        box(-1.5f, 0.62f, 2.5f, 1.5f, 1.5f, 3.0f);
        box(-1.2f, 0.62f, 3.0f, 1.2f, 1.28f, 3.5f);
        box(-0.8f, 0.62f, 3.5f, 0.8f, 1.06f, 3.95f);
        colour(246, 204, 70);
        box(-0.22f, 1.5f, 2.5f, 0.22f, 1.62f, 3.0f);
        box(-0.16f, 1.28f, 3.0f, 0.16f, 1.4f, 3.5f);
        box(-0.1f, 1.06f, 3.5f, 0.1f, 1.18f, 3.95f);
        colour(255, 246, 200);
        glow(1.4f);
        boxPair(0.72f, 0.92f, 3.5f, 1.08f, 1.2f, 3.58f);
        glow(0.0f);
    }
    else
    {
        // the cars behind have a plain rounded-off front
        colour(paint[0], paint[1], paint[2]);
        box(-1.3f, 0.62f, 2.5f, 1.3f, 1.3f, 2.75f);
    }
    if (last)
    {
        colour(255, 60, 50);
        glow(1.2f);
        boxPair(0.8f, 1.5f, -2.58f, 1.2f, 1.8f, -2.5f);
        glow(0.0f);
    }
}

// The track parameter `distance` further back along the track from u.
float stepBack(const CTrack &track, float u, float distance, TrackSplineType splineType)
{
    const float period = static_cast<float>(track.points.size());
    const float step = 0.004f;
    float remaining = distance;
    Pnt3f here = track.evaluatePosition(u, splineType);
    for (int i = 0; i < 20000 && remaining > 0.0f; ++i)
    {
        float next = u - step;
        if (next < 0.0f)
            next += period;
        const Pnt3f there = track.evaluatePosition(next, splineType);
        const float dx = there.x - here.x, dy = there.y - here.y, dz = there.z - here.z;
        const float length = sqrtf(dx * dx + dy * dy + dz * dz);
        if (length >= remaining && length > 1.0e-6f)
        {
            u -= step * (remaining / length);
            if (u < 0.0f)
                u += period;
            return u;
        }
        remaining -= length;
        u = next;
        here = there;
    }
    return u;
}
} // namespace

void TrainModel::draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time)
{
    if (track.points.size() < 4)
        return;
    gShadowPass = shadowPass;
    glPushAttrib(GL_CURRENT_BIT);
    glow(0.0f);
    float u = track.trainU;
    for (int car = 0; car < kCars; ++car)
    {
        if (car > 0)
            u = stepBack(track, u, kCarSpacing, splineType);
        const Pnt3f position = track.evaluatePosition(u, splineType);
        Pnt3f forward = track.evaluateTangent(u, splineType);
        Pnt3f side;
        Pnt3f up;
        makeTrackFrame(forward, side, up);
        const GLfloat frame[16] = {side.x,     side.y,     side.z,     0.0f,      up.x,      up.y,
                                   up.z,       0.0f,       forward.x,  forward.y, forward.z, 0.0f,
                                   position.x, position.y, position.z, 1.0f};
        glPushMatrix();
        glMultMatrixf(frame);
        drawCar(car, car == 0, car == kCars - 1, time);
        glPopMatrix();
    }
    glow(0.0f);
    glPopAttrib();
}
