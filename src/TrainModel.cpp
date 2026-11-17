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
// A steam locomotive and five wooden carts, one rider in each, so nobody's
// arms reach a neighbour or the walls.
const int kCarts = 5;
const int kVehicles = 1 + kCarts;
// The locomotive's body is built at this scale, so it stands bigger than the
// carts; its driver is not scaled, every rider is the same size.
const float kEngineScale = 1.3f;
// The seat height every rider sits at, above the top of the rails.
const float kSeatLift = 0.3f;
// Along the track, centre to centre: the locomotive's back is 2.6 units
// behind its centre, a cart is 3.4 long, the couplings about 0.7.
const float kEngineToCart = 5.0f;
const float kCartToCart = 4.0f;

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

// Wheels on both sides at z, with a bright hub.
void wheelPair(float z, float radius, int r, int g, int b)
{
    colour(26, 26, 30);
    boxPair(0.98f, 0.04f, z - radius, 1.28f, 0.04f + 2.0f * radius, z + radius);
    colour(r, g, b);
    boxPair(1.28f, 0.04f + radius * 0.6f, z - radius * 0.35f, 1.34f, 0.04f + radius * 1.4f,
            z + radius * 0.35f);
}

// A wooden mine cart, 3.4 long and 2.4 wide, with one rider. `last` gets the
// tail lamps.
void drawCart(int index, bool last, float time)
{
    // running gear: a dark bogie, wheels with iron hubs, guide wheels under the rail
    colour(52, 54, 60);
    box(-0.9f, 0.34f, -1.3f, 0.9f, 0.62f, 1.3f);
    for (int end = -1; end <= 1; end += 2)
    {
        const float z = 1.0f * static_cast<float>(end);
        wheelPair(z, 0.32f, 170, 174, 182);
        colour(140, 144, 152);
        boxPair(1.08f, -0.26f, z - 0.14f, 1.26f, 0.02f, z + 0.14f);
    }

    // the floor, then walls of three planks each between dark corner posts
    colour(96, 62, 34);
    box(-1.2f, 0.62f, -1.7f, 1.2f, 0.82f, 1.7f);
    const int planks[3][3] = {{168, 112, 60}, {184, 126, 70}, {156, 102, 54}};
    for (int k = 0; k < 3; ++k)
    {
        const int *p = planks[(k + index) % 3];
        const float y0 = 0.82f + 0.36f * static_cast<float>(k);
        const float y1 = y0 + 0.34f; // a dark seam between the planks
        colour(p[0], p[1], p[2]);
        boxPair(1.04f, y0, -1.56f, 1.2f, y1, 1.56f); // sides
        box(-1.04f, y0, 1.54f, 1.04f, y1, 1.7f);     // front
        box(-1.04f, y0, -1.7f, 1.04f, y1, -1.54f);   // back
        colour(p[0] - 50, p[1] - 40, p[2] - 26);
        boxPair(1.05f, y1, -1.56f, 1.19f, y1 + 0.02f, 1.56f);
    }
    colour(84, 52, 28);
    for (int end = -1; end <= 1; end += 2)
        boxPair(1.0f, 0.62f, 1.46f * static_cast<float>(end) - 0.24f, 1.26f, 2.02f,
                1.46f * static_cast<float>(end) + 0.24f); // corner posts
    // a dark rim round the top
    boxPair(1.02f, 1.9f, -1.74f, 1.28f, 2.04f, 1.74f);
    box(-1.28f, 1.9f, 1.5f, 1.28f, 2.04f, 1.76f);
    box(-1.28f, 1.9f, -1.76f, 1.28f, 2.04f, -1.5f);
    // iron bands down the middle of the sides and a plate on the front
    colour(120, 124, 132);
    boxPair(1.2f, 0.7f, -0.12f, 1.24f, 1.92f, 0.12f);
    box(-0.36f, 1.08f, 1.7f, 0.36f, 1.6f, 1.74f);
    colour(222, 176, 64);
    box(-0.22f, 1.18f, 1.72f, 0.22f, 1.5f, 1.76f);

    // a seat at the back, the rider in the middle and an iron lap bar
    // The seat is raised so the rider's body and waving arms show above the
    // walls, as on a ride; the arms stay well inside them.
    colour(70, 46, 28);
    box(-0.8f, 0.82f, -1.5f, 0.8f, 1.3f + kSeatLift, -0.6f);
    glPushMatrix();
    glTranslatef(0.0f, kSeatLift, 0.0f);
    drawRider(0.0f, -0.65f, index + 1, time);
    glPopMatrix();
    colour(150, 154, 162);
    box(-1.04f, 1.58f, 0.3f, 1.04f, 1.7f, 0.42f);

    // coupling to the car behind
    if (!last)
    {
        colour(60, 62, 70);
        box(-0.12f, 0.42f, -2.3f, 0.12f, 0.58f, -1.7f);
    }
    else
    {
        colour(255, 60, 50);
        glow(1.2f);
        boxPair(0.6f, 1.3f, -1.78f, 0.95f, 1.6f, -1.7f);
        glow(0.0f);
    }
}

// The steam locomotive: a red boiler with brass bands and dome, a black
// smokebox with the head lamp, a chimney, three driving wheels a side with
// their rod, and a green cab with a grey roof and the driver. Modelled 5
// long, from z = -2 (the back of the cab) to z = 3 (the buffer beam), and
// drawn kEngineScale times that size.
void drawLocomotive(float time)
{
    glPushMatrix();
    glScalef(kEngineScale, kEngineScale, kEngineScale);
    // frame and running gear
    colour(30, 30, 34);
    box(-1.0f, 0.34f, -2.0f, 1.0f, 0.72f, 3.0f);
    colour(184, 36, 34);
    boxPair(0.86f, 0.62f, -2.0f, 1.32f, 0.9f, 3.0f); // running boards
    for (int k = 0; k < 3; ++k)
        wheelPair(-1.2f + 1.6f * static_cast<float>(k), 0.42f, 196, 44, 40);
    colour(150, 154, 162);
    boxPair(1.34f, 0.38f, -1.3f, 1.4f, 0.5f, 2.1f); // coupling rod
    colour(140, 144, 152);
    boxPair(1.08f, -0.26f, -0.15f, 1.26f, 0.02f, 0.15f); // guide wheels
    // cowcatcher and buffer beam
    colour(60, 62, 68);
    box(-1.1f, 0.18f, 3.0f, 1.1f, 0.6f, 3.3f);
    colour(184, 36, 34);
    box(-1.32f, 0.6f, 2.96f, 1.32f, 0.92f, 3.12f);
    colour(30, 30, 34);
    boxPair(0.7f, 0.66f, 3.12f, 1.0f, 0.86f, 3.3f); // buffers

    // boiler with brass bands, dome and a black smokebox at the front
    colour(196, 40, 36);
    box(-0.9f, 0.9f, 0.0f, 0.9f, 2.3f, 2.6f);
    colour(232, 182, 60);
    for (int k = 0; k < 3; ++k)
    {
        const float z = 0.5f + 0.85f * static_cast<float>(k);
        box(-0.95f, 0.88f, z, 0.95f, 2.35f, z + 0.14f);
    }
    box(-0.34f, 2.3f, 1.0f, 0.34f, 2.72f, 1.6f); // dome
    colour(36, 36, 40);
    box(-1.0f, 0.88f, 2.6f, 1.0f, 2.42f, 3.0f);
    box(-0.3f, 2.4f, 2.1f, 0.3f, 3.4f, 2.6f);     // chimney
    box(-0.42f, 3.3f, 1.98f, 0.42f, 3.6f, 2.72f); // and its lip
    // the head lamp in a brass frame
    colour(232, 182, 60);
    box(-0.44f, 1.3f, 3.0f, 0.44f, 2.12f, 3.06f);
    colour(255, 246, 200);
    glow(1.5f);
    box(-0.3f, 1.44f, 3.04f, 0.3f, 1.98f, 3.1f);
    glow(0.0f);

    // the cab: green walls with windows, open at the back, a grey roof
    colour(48, 128, 60);
    boxPair(1.0f, 0.9f, -2.0f, 1.26f, 1.7f, 0.0f);   // lower sides
    boxPair(1.0f, 1.7f, -2.0f, 1.26f, 2.72f, -1.7f); // back posts
    boxPair(1.0f, 1.7f, -0.3f, 1.26f, 2.72f, 0.0f);  // front posts
    box(-1.26f, 0.9f, -0.22f, 1.26f, 2.72f, 0.0f);   // front wall
    colour(232, 182, 60);
    boxPair(1.26f, 0.9f, -2.0f, 1.3f, 1.04f, 0.0f); // brass trim
    colour(44, 52, 60);
    boxPair(0.34f, 1.98f, 0.0f, 0.82f, 2.46f, 0.03f); // front windows
    colour(122, 126, 134);
    box(-1.48f, 2.72f, -2.3f, 1.48f, 2.9f, 0.24f); // roof
    box(-1.0f, 2.9f, -2.3f, 1.0f, 3.0f, 0.24f);
    // coupling to the first cart
    colour(60, 62, 70);
    box(-0.12f, 0.42f, -2.6f, 0.12f, 0.58f, -2.0f);
    glPopMatrix();

    // The driver, at the riders' size and seat height: the bigger cab has
    // room above the head and the waving hands.
    colour(70, 46, 28);
    box(-0.8f, 0.9f * kEngineScale, -2.4f, 0.8f, 1.3f + kSeatLift, -1.5f);
    glPushMatrix();
    glTranslatef(0.0f, kSeatLift, 0.0f);
    drawRider(0.0f, -1.5f, 0, time);
    glPopMatrix();
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
        const GLfloat frame[16] = {side.x,     side.y,     side.z,     0.0f,      up.x,      up.y,
                                   up.z,       0.0f,       forward.x,  forward.y, forward.z, 0.0f,
                                   position.x, position.y, position.z, 1.0f};
        glPushMatrix();
        glMultMatrixf(frame);
        if (car == 0)
            drawLocomotive(time);
        else
            drawCart(car - 1, car == kVehicles - 1, time);
        glPopMatrix();
    }
    glow(0.0f);
    glPopAttrib();
}
