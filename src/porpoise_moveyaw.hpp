/* Porpoise - the PS3 Eye keeping the Move's aim from drifting.
 *
 * The Move's gyroscope knows how it turns but not, over time, which way it
 * faces: left and right drift, faster after hard swings, and the pointer
 * wanders until the Move is centred again. The camera sees where the ball is,
 * not which way it points, but as the Move moves, the way the camera sees the
 * ball go and the way the Move's accelerometer felt it go are the same
 * motion: the turn between the two is how far the Move's idea of "ahead" has
 * drifted. Measured over the swings and sweeps of play, it is eased back out,
 * with no centring (Oct 24, Ruben: "it should remember where the ball is").
 *
 * One estimator, the Move's (player 1). Times in seconds on one clock.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

namespace porpoise::moveyaw
{
/* A motion sample: when, the Move's orientation (aim::Fusion::q, its axes to
 * the world's, z up), its accelerometer (g) and gyroscope (rad/s, its drift
 * taken off), in its own axes. */
void imu(double t, const float q[4], const float accel_g[3], const float gyro[3]);
/* Where the camera saw the ball: its middle and radius in the 640 x 480
 * picture, when it arrived (the picture's own delay is taken off here). */
void camera(double t, float x, float y, float radius);
/* The turn to give the Move's orientation now (radians about the world's up,
 * aim::turn_world), taken: 0 most of the time. */
float take_correction();
/* Centred: the count of what's been taken out starts again (the camera's
 * "ahead" stays as it was). */
void centred();
/* The Move or the camera went: start over. */
void reset();

/* For the logs: how sure it is (0..1), its swings counted, the drift it has
 * taken out since the last centring (radians). */
struct Report
{
    float confidence = 0;
    int pairs = 0;
    float corrected = 0;
    float error = 0; /* the drift it saw last (radians) */
    bool anchored = false;
    bool mirrored = false; /* the camera's picture a mirror's */
};
Report report();
} // namespace porpoise::moveyaw
