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

// A rider in the block-figure style of sandbox games: the head, the body,
// each arm and each leg is one box, with a face of square pixels. Seated,
// so the legs point forward, and both arms are up, waving.
void drawRider(float x, float z, int who, float time)
{
    const int shirts[6][3] = {{38, 170, 176}, {214, 60, 60},  {250, 196, 50},
                              {90, 170, 80},  {150, 90, 200}, {240, 240, 240}};
    const int trousers[4][3] = {{58, 60, 150}, {70, 70, 80}, {120, 84, 50}, {40, 100, 120}};
    const int hairs[4][3] = {{70, 46, 30}, {30, 30, 34}, {214, 170, 80}, {150, 70, 40}};
    const int skins[3][3] = {{224, 172, 136}, {246, 206, 172}, {160, 108, 76}};
    const int *shirt = shirts[who % 6];
    const int *leg = trousers[(who / 2 + who) % 4];
    const int *hair = hairs[(who * 3 + 1) % 4];
    const int *skin = skins[(who * 5 + who / 3) % 3];
    const float unit = 0.07f; // one pixel of the figure

    // legs: two boxes along the seat, with shoes
    for (int side = -1; side <= 1; side += 2)
    {
        const float lx = x + 0.14f * static_cast<float>(side);
        colour(leg[0], leg[1], leg[2]);
        box(lx - 0.13f, 1.3f, z - 0.05f, lx + 0.13f, 1.56f, z + 0.6f);
        colour(70, 70, 76);
        box(lx - 0.13f, 1.3f, z + 0.6f, lx + 0.13f, 1.56f, z + 0.74f);
    }
    // body
    colour(shirt[0], shirt[1], shirt[2]);
    box(x - 0.28f, 1.5f, z - 0.2f, x + 0.28f, 2.3f, z + 0.08f);
    // head: a cube, hair over the top, back and sides
    const float hy = 2.3f, hz = z - 0.34f; // bottom and back of the head
    colour(skin[0], skin[1], skin[2]);
    box(x - 0.28f, hy, hz, x + 0.28f, hy + 0.56f, hz + 0.56f);
    colour(hair[0], hair[1], hair[2]);
    box(x - 0.295f, hy + 0.42f, hz - 0.015f, x + 0.295f, hy + 0.575f, hz + 0.575f);
    box(x - 0.295f, hy + 0.14f, hz - 0.015f, x + 0.295f, hy + 0.42f, hz + 0.3f);
    // the face, one pixel deep: white eyes with dark pupils and a mouth
    const float face = hz + 0.56f;
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = static_cast<float>(side);
        const float inner = x + s * unit, outer = x + s * 3.0f * unit, mid = x + s * 2.0f * unit;
        colour(250, 250, 250);
        box(fminf(mid, outer), hy + 3.0f * unit, face, fmaxf(mid, outer), hy + 4.0f * unit,
            face + 0.012f);
        colour(60, 50, 120);
        box(fminf(inner, mid), hy + 3.0f * unit, face, fmaxf(inner, mid), hy + 4.0f * unit,
            face + 0.012f);
    }
    colour(120, 60, 50);
    box(x - unit, hy + unit, face, x + unit, hy + 2.0f * unit, face + 0.012f);

    // arms: one box each, sleeve and hand, up in the air and waving
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = static_cast<float>(side);
        const float wave = sinf(time * 6.0f + static_cast<float>(who) * 1.7f + s) * 14.0f;
        glPushMatrix();
        glTranslatef(x + 0.4f * s, 2.16f, z - 0.06f);
        glRotatef(-16.0f * s + wave, 0.0f, 0.0f, 1.0f);
        colour(shirt[0], shirt[1], shirt[2]);
        box(-0.12f, -0.12f, -0.12f, 0.12f, 0.3f, 0.12f);
        colour(skin[0], skin[1], skin[2]);
        box(-0.12f, 0.3f, -0.12f, 0.12f, 0.72f, 0.12f);
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
