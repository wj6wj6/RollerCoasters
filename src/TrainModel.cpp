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
// A classic coaster train in the block style: a lead car with a stepped
// nose and five cars behind it, two riders side by side in each.
const int kCarts = 5;
const int kVehicles = 1 + kCarts;
// The seat height every rider sits at, above the top of the rails.
const float kSeatLift = 0.3f;
// Along the track, centre to centre: a car is 3.4 long (the lead car's
// nose adds 0.8 in front), the couplings about 0.6.
const float kEngineToCart = 4.0f;
const float kCartToCart = 4.0f;

// The livery: deep red bodies, a cream band along the sides, gold trim,
// chrome bars and a dark chassis; black padded seats.
const int kBody[3] = {170, 30, 38};
const int kBand[3] = {238, 226, 196};
const int kGold[3] = {226, 178, 64};
const int kChrome[3] = {198, 202, 210};
const int kChassis[3] = {40, 42, 48};
const int kSeat[3] = {32, 32, 38};

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

// How a rider sits at this moment, worked out from the track under their
// car (TrainModel::draw()), so they react to the ride: hands on the bar on
// the climbs, arms up on the drops, leaning out on the curves, looking
// round on the level.
struct Pose
{
    float arm[2];    // each arm (left, right): 0 straight up .. 125 hands on the bar, degrees
    float spread[2]; // and out to the side, degrees
    float lean;      // the body to the side, degrees (positive: to +x)
    float pitch;     // and back (negative) or forward, degrees
    float headYaw;   // the head turned, degrees
    float headPitch; // and tilted up (negative) or down
};

void colour3(const int c[3], int darken = 0)
{
    colour(c[0] - darken, c[1] - darken, c[2] - darken);
}

// A rider in the block-figure style of sandbox games: the head, the body,
// each arm and each leg is one box, with a face of square pixels; seated,
// the legs forward. The upper body turns about the hips, the head about
// the neck and the arms about the shoulders, as the pose says.
void drawRider(float x, float z, int who, const Pose &pose)
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
        const float lx = x + 0.12f * static_cast<float>(side);
        colour(leg[0], leg[1], leg[2]);
        box(lx - 0.11f, 1.3f, z - 0.05f, lx + 0.11f, 1.54f, z + 0.6f);
        colour(70, 70, 76);
        box(lx - 0.11f, 1.3f, z + 0.6f, lx + 0.11f, 1.54f, z + 0.72f);
    }

    glPushMatrix();
    glTranslatef(x, 1.5f, z - 0.06f); // the hips
    glRotatef(pose.lean, 0.0f, 0.0f, -1.0f);
    glRotatef(pose.pitch, 1.0f, 0.0f, 0.0f);
    // body
    colour(shirt[0], shirt[1], shirt[2]);
    box(-0.25f, 0.0f, -0.14f, 0.25f, 0.78f, 0.14f);
    // head: a cube, hair over the top, back and sides
    glPushMatrix();
    glTranslatef(0.0f, 0.78f, 0.0f); // the neck
    glRotatef(pose.headYaw, 0.0f, 1.0f, 0.0f);
    glRotatef(pose.headPitch, 1.0f, 0.0f, 0.0f);
    const float hz = -0.27f; // the back of the head
    colour(skin[0], skin[1], skin[2]);
    box(-0.26f, 0.0f, hz, 0.26f, 0.52f, hz + 0.52f);
    colour(hair[0], hair[1], hair[2]);
    box(-0.275f, 0.39f, hz - 0.015f, 0.275f, 0.535f, hz + 0.535f);
    box(-0.275f, 0.13f, hz - 0.015f, 0.275f, 0.39f, hz + 0.28f);
    // the face, one pixel deep: white eyes with dark pupils and a mouth
    const float face = hz + 0.52f;
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = static_cast<float>(side);
        const float inner = s * unit, outer = s * 3.0f * unit, mid = s * 2.0f * unit;
        colour(250, 250, 250);
        box(fminf(mid, outer), 3.0f * unit, face, fmaxf(mid, outer), 4.0f * unit, face + 0.012f);
        colour(60, 50, 120);
        box(fminf(inner, mid), 3.0f * unit, face, fmaxf(inner, mid), 4.0f * unit, face + 0.012f);
    }
    colour(120, 60, 50);
    box(-unit, unit, face, unit, 2.0f * unit, face + 0.012f);
    glPopMatrix();

    // arms: sleeve and hand, turned about the shoulder
    for (int k = 0; k < 2; ++k)
    {
        const float s = k == 0 ? -1.0f : 1.0f;
        glPushMatrix();
        glTranslatef(0.36f * s, 0.66f, 0.0f); // the shoulder
        glRotatef(pose.spread[k] * s, 0.0f, 0.0f, -1.0f);
        glRotatef(pose.arm[k], 1.0f, 0.0f, 0.0f);
        colour(shirt[0], shirt[1], shirt[2]);
        box(-0.1f, -0.1f, -0.1f, 0.1f, 0.3f, 0.1f);
        colour(skin[0], skin[1], skin[2]);
        box(-0.1f, 0.3f, -0.1f, 0.1f, 0.68f, 0.1f);
        glPopMatrix();
    }
    glPopMatrix();
}

// The running gear of a coaster car under one end (z): a dark axle beam,
// load wheels on top of the rails, up-stop wheels under them and guide
// wheels at the side, each with a chrome hub.
void bogie(float z)
{
    colour3(kChassis);
    box(-1.0f, 0.3f, z - 0.3f, 1.0f, 0.5f, z + 0.3f);
    colour(26, 26, 30);
    boxPair(0.98f, 0.04f, z - 0.3f, 1.24f, 0.62f, z + 0.3f);    // load wheels
    boxPair(1.02f, -0.3f, z - 0.16f, 1.22f, -0.02f, z + 0.16f); // up-stop wheels
    colour3(kChrome);
    boxPair(1.24f, 0.22f, z - 0.1f, 1.3f, 0.42f, z + 0.1f); // hubs
    colour3(kChassis, 8);
    boxPair(0.86f, -0.32f, z - 0.24f, 0.98f, 0.5f, z + 0.24f); // the side frames
}

// One coaster car, 3.4 long and 2.5 wide, with two riders. The lead car
// (lead) has a stepped nose in front with a head lamp and an emblem; the
// last (last) has tail lamps. `who` numbers its riders.
void drawCar(bool lead, bool last, int who, const Pose pose[2])
{
    bogie(1.05f);
    bogie(-1.05f);
    // the floor pan and the body: a deep red tub, a cream band along the
    // sides between gold lines, the top edge rounded off in a step
    colour3(kChassis);
    box(-1.12f, 0.5f, -1.7f, 1.12f, 0.7f, 1.7f);
    colour3(kBody);
    boxPair(1.0f, 0.7f, -1.7f, 1.24f, 1.36f, 1.7f);  // sides
    box(-1.0f, 0.7f, -1.7f, 1.0f, 1.36f, -1.48f);    // back
    box(-1.0f, 0.7f, 1.38f, 1.0f, 1.62f, 1.7f);      // front, a little higher
    boxPair(1.0f, 1.62f, -1.7f, 1.16f, 1.78f, 1.7f); // the upper sides, set in
    colour3(kBand);
    boxPair(1.24f, 0.98f, -1.66f, 1.27f, 1.18f, 1.66f);
    colour3(kGold);
    boxPair(1.24f, 0.94f, -1.68f, 1.28f, 0.98f, 1.68f);
    boxPair(1.24f, 1.18f, -1.68f, 1.28f, 1.22f, 1.68f);
    colour3(kBody, 30);
    boxPair(1.0f, 1.36f, -1.7f, 1.22f, 1.62f, 1.7f); // the step under the rim
    colour3(kGold);
    boxPair(0.98f, 1.78f, -1.72f, 1.18f, 1.84f, 1.72f); // the rim
    box(-1.18f, 1.62f, 1.62f, 1.18f, 1.7f, 1.74f);

    // the seat: a black padded bench with a high back and headrests, and a
    // chrome lap bar in front
    colour3(kSeat);
    box(-0.96f, 0.7f, -1.4f, 0.96f, 1.3f + kSeatLift, -0.5f);
    box(-0.96f, 1.3f, -1.48f, 0.96f, 2.3f, -1.18f);
    for (int k = -1; k <= 1; k += 2)
        box(0.5f * static_cast<float>(k) - 0.3f, 2.3f, -1.48f, 0.5f * static_cast<float>(k) + 0.3f,
            2.66f, -1.24f);
    colour3(kChrome);
    box(-0.96f, 1.98f + kSeatLift, -0.22f, 0.96f, 2.08f + kSeatLift, -0.1f);
    boxPair(0.9f, 1.3f, -0.2f, 0.98f, 2.04f + kSeatLift, -0.12f);

    for (int k = 0; k < 2; ++k)
    {
        glPushMatrix();
        glTranslatef(0.0f, kSeatLift, 0.0f);
        drawRider(k == 0 ? -0.5f : 0.5f, -0.62f, who + k, pose[k]);
        glPopMatrix();
    }

    if (lead)
    {
        // the nose, in steps down to a chrome bumper, a head lamp and a
        // gold star on its front
        colour3(kBody);
        box(-1.24f, 0.7f, 1.7f, 1.24f, 1.36f, 2.1f);
        box(-1.08f, 0.6f, 2.1f, 1.08f, 1.12f, 2.4f);
        box(-0.86f, 0.56f, 2.4f, 0.86f, 0.9f, 2.56f);
        colour3(kBand);
        box(-1.24f, 0.98f, 2.1f, 1.24f, 1.18f, 2.12f);
        colour3(kGold);
        box(-1.0f, 1.12f, 2.1f, 1.0f, 1.18f, 2.34f);
        colour3(kChrome);
        box(-1.0f, 0.4f, 2.5f, 1.0f, 0.56f, 2.66f);
        colour3(kGold);
        box(-0.34f, 0.92f, 2.4f, 0.34f, 1.08f, 2.44f);
        box(-0.12f, 0.8f, 2.4f, 0.12f, 1.2f, 2.44f);
        colour(255, 246, 210);
        glow(1.5f);
        box(-0.26f, 0.62f, 2.54f, 0.26f, 0.84f, 2.6f);
        glow(0.0f);
    }
    else
    {
        // a small number plate on the front
        colour3(kGold);
        box(-0.2f, 1.0f, 1.7f, 0.2f, 1.3f, 1.73f);
    }
    if (last)
    {
        colour(255, 60, 50);
        glow(1.2f);
        boxPair(0.62f, 1.0f, -1.74f, 0.96f, 1.28f, -1.7f);
        glow(0.0f);
    }
    else
    {
        // the coupling to the car behind
        colour3(kChassis);
        box(-0.12f, 0.36f, -2.3f, 0.12f, 0.5f, -1.7f);
    }
}

// How the riders of a car sit, from the track under it: climb is the
// slope it points along (the y of its unit direction), turn how fast that
// direction swings to the side (positive: to +x) per unit along the track.
void posesFor(int car, float climb, float turn, float time, Pose pose[2])
{
    // how steeply the car climbs or drops, 0..1
    const float up = fminf(fmaxf((climb - 0.08f) / 0.25f, 0.0f), 1.0f);
    const float down = fminf(fmaxf((-climb - 0.08f) / 0.27f, 0.0f), 1.0f);
    const float level = 1.0f - fmaxf(up, down);
    // they lean out on a curve, as it throws them, but not over the side
    const float lean = fminf(fmaxf(-turn * 220.0f, -16.0f), 16.0f);
    for (int k = 0; k < 2; ++k)
    {
        const int who = car * 2 + k;
        const float phase = static_cast<float>((who * 37) % 17);
        // a third of them keep their hands on the bar even on the drops
        const bool brave = who % 3 != 1;
        const float thrill = down * (brave ? 1.0f : 0.25f);
        Pose &p = pose[k];
        for (int arm = 0; arm < 2; ++arm)
        {
            const float sway = sinf(time * 5.0f + phase + static_cast<float>(arm) * 1.9f) * 9.0f;
            p.arm[arm] = 125.0f + (8.0f - 125.0f) * thrill + sway * thrill;
            p.spread[arm] = 6.0f + 14.0f * thrill;
        }
        // on the level now and then one waves
        const float wave = level * fmaxf(sinf(time * 0.6f + phase) - 0.7f, 0.0f) / 0.3f;
        const int hand = who % 2;
        p.arm[hand] += (14.0f - p.arm[hand]) * wave;
        p.spread[hand] += (24.0f + sinf(time * 7.0f + phase) * 14.0f - p.spread[hand]) * wave;
        p.lean = lean;
        p.pitch = 8.0f * up - 12.0f * down;
        // they look up the climbs, round them on the level
        p.headYaw = level * sinf(time * 0.45f + phase) * 30.0f;
        p.headPitch = -18.0f * up + 6.0f * down;
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

float TrainModel::length()
{
    return kEngineToCart + static_cast<float>(kCarts - 1) * kCartToCart;
}

float TrainModel::parameterBehind(const CTrack &track, float u, float distance,
                                  TrackSplineType splineType)
{
    return stepBack(track, u, distance, splineType);
}

void TrainModel::draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time)
{
    if (track.points.size() < 4)
        return;
    gShadowPass = shadowPass;
    glPushAttrib(GL_CURRENT_BIT);
    glow(0.0f);
    float u = track.trainU;
    for (int car = 0; car < kVehicles; ++car)
    {
        if (car > 0)
            u = stepBack(track, u, car == 1 ? kEngineToCart : kCartToCart, splineType);
        const Pnt3f position = track.evaluatePosition(u, splineType);
        Pnt3f forward = track.evaluateTangent(u, splineType);
        Pnt3f side;
        Pnt3f up;
        makeTrackFrame(forward, side, up);
        // how the track bends under the car, for the riders: its slope,
        // and how fast it turns to the side over the next stretch
        const float period = static_cast<float>(track.points.size());
        float ahead = u + 0.05f;
        if (ahead >= period)
            ahead -= period;
        Pnt3f next = track.evaluateTangent(ahead, splineType);
        next.normalize();
        Pnt3f dir = forward;
        dir.normalize();
        const Pnt3f here = track.evaluatePosition(ahead, splineType);
        const float dx = here.x - position.x, dy = here.y - position.y, dz = here.z - position.z;
        const float stretch = fmaxf(sqrtf(dx * dx + dy * dy + dz * dz), 0.01f);
        const float turn =
            ((next.x - dir.x) * side.x + (next.y - dir.y) * side.y + (next.z - dir.z) * side.z) /
            stretch;
        Pose pose[2];
        posesFor(car, dir.y, turn, time, pose);
        const GLfloat frame[16] = {side.x,     side.y,     side.z,     0.0f,      up.x,      up.y,
                                   up.z,       0.0f,       forward.x,  forward.y, forward.z, 0.0f,
                                   position.x, position.y, position.z, 1.0f};
        glPushMatrix();
        glMultMatrixf(frame);
        drawCar(car == 0, car == kVehicles - 1, car * 2, pose);
        glPopMatrix();
    }
    glow(0.0f);
    glPopAttrib();
}
