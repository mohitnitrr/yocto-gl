//
// LICENSE:
//
// Copyright (c) 2016 -- 2017 Fabio Pellacini
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR YOCTO_SYM
// -----------------------------------------------------------------------------

#include "yocto_sym.h"

#ifndef YSYM_NO_BVH
#include "yocto_bvh.h"
#endif

#include <iostream>
#include <map>

//
// TODO: cleanup: frame -> pos/rot
// TODO: cleanup: shape -> body/shape/material
//

namespace ysym {

//
// Rigid shape.
//
struct shape {
    // shape data ----------------------------------
    int nelems = 0;                        // numner of elements
    const ym::vec3i* triangles = nullptr;  // triangles
    int nverts = 0;                        // number of vertices
    const ym::vec3f* pos = nullptr;        // vertex positions

    // [private] computed values ------------------
    float _volume = 1;                                  // volume
    ym::mat3f _inertia_local = ym::identity_mat3f;      // inertia
    ym::vec3f _centroid_local = ym::zero3f;             // center
    ym::mat3f _inertia_inv_local = ym::identity_mat3f;  // inverse of inertia
};

//
// Material
//
struct material {
    float density = 0;
};

//
// Rigid body
//
struct rigid_body {
    // shape configuration ------------------------
    ym::frame3f frame;  // rigid transform
    ym::vec3f lin_vel;  // linear velocity
    ym::vec3f ang_vel;  // angular velocity

    // physical properties ------------------------
    material* mat = nullptr;  // material
    bool simulated = true;    // simulated

    // shape properties ---------------------------
    shape* shp = nullptr;  // shape

    // [private] computed values ------------------
    float _mass = 1;                         // mass
    ym::vec3f _centroid_world = ym::zero3f;  // shape center
    float _mass_inv = 1;                     // mass inverse
    ym::mat3f _inertia_inv_world =
        ym::identity_mat3f;  // inverse of inertia tensor (world-space)
};

//
// Collision point and response [private]
//
struct collision {
    rigid_body *bdy1 = nullptr, *bdy2 = nullptr;  // bodies
    ym::frame3f frame = ym::identity_frame3f;     // collision frame
    ym::vec3f impulse = ym::zero3f, local_impulse = ym::zero3f;  // impulses
    ym::vec3f meff_inv = ym::zero3f;  // effective mass
    float depth = 0;                  // penetration depth
    ym::vec3f r1 = ym::zero3f, r2 = ym::zero3f;
};

//
// Rigid body scene
//
struct scene {
    // simulation shapes -----------------------
    std::vector<material*> materials;  // materials
    std::vector<shape*> shapes;        // shapes
    std::vector<rigid_body*> bodies;   // bodies

    // overlap callbacks -----------------------
    float overlap_max_radius = 0.25;  // maximum vertex overlap distance
    overlap_shapes_cb overlap_shapes = nullptr;  // overlap callbacks
    overlap_shape_cb overlap_shape = nullptr;    // overlap callbacks
    overlap_refit_cb overlap_refit = nullptr;    // overlap callbacks
#ifndef YSYM_NO_BVH
    ybvh::scene* overlap_bvh = nullptr;  // overlapoverlap internal bvh
#endif

    // overlap data used for visualization [private] ----
    std::vector<collision> __collisions;

    // destructor
    ~scene() {
        for (auto shp : shapes)
            if (shp) delete shp;
        for (auto bdy : bodies)
            if (bdy) delete bdy;
        for (auto mat : materials)
            if (mat) delete mat;
#ifndef YTRACE_NO_BVH
        if (overlap_bvh) ybvh::free_scene(overlap_bvh);
#endif
    }
};

//
// Public API.
//
scene* make_scene() {
    auto scn = new scene();
    return scn;
}

//
// Public API.
//
void free_scene(scene*& scn) {
    if (scn) delete scn;
    scn = nullptr;
}

//
// Public API.
//
int add_rigid_shape(scene* scn, int ntriangles, const ym::vec3i* triangles,
    int nverts, const ym::vec3f* pos) {
    auto shp = new shape();
    shp->nelems = ntriangles;
    shp->triangles = triangles;
    shp->nverts = nverts;
    shp->pos = pos;
    scn->shapes.push_back(shp);
    return (int)scn->shapes.size() - 1;
}

//
// Public API.
//
int add_rigid_body(scene* scn, const ym::frame3f& frame, int sid, int mid,
    const ym::vec3f& lin_vel, const ym::vec3f& ang_vel) {
    auto bdy = new rigid_body();
    bdy->frame = frame;
    bdy->shp = scn->shapes[sid];
    bdy->mat = scn->materials[mid];
    bdy->frame = frame;
    bdy->lin_vel = lin_vel;
    bdy->ang_vel = ang_vel;
    bdy->simulated = scn->materials[mid]->density > 0;
    scn->bodies.push_back(bdy);
    return (int)scn->bodies.size() - 1;
}

//
// Public API.
//
int add_rigid_material(scene* scn, float density) {
    auto mat = new material();
    mat->density = density;
    scn->materials.push_back(mat);
    return (int)scn->materials.size() - 1;
}

//
// Public API.
//
ym::frame3f get_rigid_body_frame(const scene* scn, int bid) {
    return scn->bodies[bid]->frame;
}

//
// Public API.
//
std::pair<ym::vec3f, ym::vec3f> get_rigid_body_velocity(
    const scene* scn, int bid) {
    return {scn->bodies[bid]->lin_vel, scn->bodies[bid]->ang_vel};
}

//
// Public API.
//
void set_rigid_body_frame(scene* scn, int bid, const ym::frame3f& frame) {
    scn->bodies[bid]->frame = frame;
}

//
// Public API.
//
void set_rigid_body_velocity(
    scene* scn, int bid, const ym::vec3f& lin_vel, const ym::vec3f& ang_vel) {
    scn->bodies[bid]->lin_vel = lin_vel;
    scn->bodies[bid]->ang_vel = ang_vel;
}

//
// Public API.
//
void set_overlap_callbacks(scene* scn, overlap_shapes_cb overlap_shapes,
    overlap_shape_cb overlap_shape, overlap_refit_cb overlap_refit) {
    scn->overlap_shapes = overlap_shapes;
    scn->overlap_shape = overlap_shape;
    scn->overlap_refit = overlap_refit;
}

//
// Initialize overlap functions using internal structures.
//
void init_overlap(scene* scn) {
#ifndef YSYM_NO_BVH
    scn->overlap_bvh = ybvh::make_scene();
    auto shape_map = std::map<shape*, int>();
    for (auto shp : scn->shapes) {
        if (!shp->triangles) {
            shape_map[shp] = ybvh::add_point_shape(
                scn->overlap_bvh, shp->nverts, shp->pos, nullptr);
        } else {
            shape_map[shp] = ybvh::add_triangle_shape(scn->overlap_bvh,
                shp->nelems, shp->triangles, shp->nverts, shp->pos, nullptr);
        }
    }
    for (auto bdy : scn->bodies) {
        ybvh::add_instance(
            scn->overlap_bvh, bdy->frame, shape_map.at(bdy->shp));
    }
    ybvh::build_scene_bvh(scn->overlap_bvh);
    set_overlap_callbacks(scn,
        [scn](std::vector<ym::vec2i>* overlaps) {
            ybvh::overlap_instance_bounds(
                scn->overlap_bvh, scn->overlap_bvh, true, true, overlaps);
        },
        [scn](int iid, const ym::vec3f& pt, float max_dist) {
            auto overlap = ybvh::overlap_instance(
                scn->overlap_bvh, iid, pt, max_dist, false);
            ysym::overlap_point opt;
            opt.dist = overlap.dist;
            opt.iid = overlap.iid;
            opt.sid = overlap.sid;
            opt.eid = overlap.eid;
            opt.euv = overlap.euv;
            return opt;
        },
        [scn](const scene* scn, int nshapes) {
            for (auto iid = 0; iid < nshapes; iid++) {
                ybvh::set_instance_frame(
                    scn->overlap_bvh, iid, get_rigid_body_frame(scn, iid));
            }
            ybvh::refit_scene_bvh(scn->overlap_bvh, false);
        });
#endif
}

//
// Computes the tetrahedra moment of inertia
//
// Notes:
// - from "Explicit Exact Formulas for the 3-D Tetrahedron Inertia Tensor
// in Terms of its Vertex Coordinates" by F. Tonon published in Journal of
// Mathematics and Statistics 1 (1), 2004.
// - implemented similarly to
// https://github.com/melax/sandbox/blob/master/include/geometric.h
//
static ym::mat3f _compute_tetra_inertia(const ym::vec3f& v0,
    const ym::vec3f& v1, const ym::vec3f& v2, const ym::vec3f& v3,
    const ym::vec3f& center) {
    // volume
    auto volume = ym::tetrahedron_volume(v0, v1, v2, v3);
    // relative vertices
    auto vr0 = v0 - center, vr1 = v1 - center, vr2 = v2 - center,
         vr3 = v3 - center;
    // diagonal elements
    auto diag = ym::zero3f;  // x^2, y^2, z^2
    for (auto j = 0; j < 3; j++) {
        diag[j] = (vr0[j] * vr0[j] + vr1[j] * vr1[j] + vr2[j] * vr2[j] +
                      vr3[j] * vr3[j] + vr0[j] * vr1[j] + vr0[j] * vr2[j] +
                      vr0[j] * vr3[j] + vr1[j] * vr2[j] + vr1[j] * vr3[j] +
                      vr2[j] * vr3[j]) *
                  6 * volume / 60;
    }
    // off-diagonal elements
    auto offd = ym::zero3f;  // x*y, x*z, y*z
    for (auto j = 0; j < 3; j++) {
        auto j1 = (j + 1) % 3, j2 = (j + 2) % 3;
        offd[j] =
            (2 * vr0[j1] * vr0[j2] + 2 * vr1[j1] * vr1[j2] +
                2 * vr2[j1] * vr2[j2] + 2 * vr3[j1] * vr3[j2] +
                vr1[j1] * vr0[j2] + vr2[j1] * vr0[j2] + vr3[j1] * vr0[j2] +
                vr0[j1] * vr1[j2] + vr2[j1] * vr1[j2] + vr3[j1] * vr1[j2] +
                vr0[j1] * vr2[j2] + vr1[j1] * vr2[j2] + vr3[j1] * vr2[j2] +
                vr0[j1] * vr3[j2] + vr1[j1] * vr3[j2] + vr2[j1] * vr3[j2]) *
            6 * volume / 120;
    }
    // setup inertia
    return {{diag.y + diag.z, -offd.z, -offd.y},
        {-offd.z, diag.x + diag.z, -offd.x},
        {-offd.y, -offd.x, diag.x + diag.y}};
}

//
// Public API.
//
std::tuple<float, ym::vec3f, ym::mat3f> _compute_moments(int ntriangles,
    const ym::vec3i* triangles, int nverts, const ym::vec3f* pos) {
    // volume and center
    auto volume = 0.0f;
    auto center = ym::zero3f;
    for (auto i = 0; i < ntriangles; i++) {
        auto t = triangles[i];
        auto tvolume =
            ym::tetrahedron_volume(ym::zero3f, pos[t.x], pos[t.y], pos[t.z]);
        volume += tvolume;
        center +=
            tvolume * (ym::zero3f + pos[t.x] + pos[t.y] + pos[t.z]) / 4.0f;
    }
    center /= volume;
    // inertia
    auto inertia = ym::mat3f{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (auto i = 0; i < ntriangles; i++) {
        auto t = triangles[i];
        inertia += _compute_tetra_inertia(
            ym::zero3f, pos[t.x], pos[t.y], pos[t.z], center);
    }
    inertia /= volume;
    return std::make_tuple(volume, center, inertia);
}

//
// Compute moments. Public API, see above.
//
std::tuple<float, ym::vec3f, ym::mat3f> _compute_moments(
    int ntetra, const ym::vec4i* tetra, int nverts, const ym::vec3f* pos) {
    // volume and center
    auto volume = 0.0f;
    auto center = ym::zero3f;
    for (auto i = 0; i < ntetra; i++) {
        auto t = tetra[i];
        auto tvolume =
            ym::tetrahedron_volume(pos[t.x], pos[t.y], pos[t.z], pos[t[3]]);
        volume += tvolume;
        center += tvolume * (pos[t.x] + pos[t.y] + pos[t.z] + pos[t[3]]) / 4.0f;
    }
    center /= volume;
    // inertia
    auto inertia = ym::mat3f{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (auto i = 0; i < ntetra; i++) {
        auto t = tetra[i];
        inertia += _compute_tetra_inertia(
            pos[t.x], pos[t.y], pos[t.z], pos[t[3]], center);
    }
    inertia /= volume;
    return std::make_tuple(volume, center, inertia);
}

//
// Public API.
//
std::tuple<float, ym::vec3f, ym::mat3f> compute_moments(int ntriangles,
    const ym::vec3i* triangles, int nverts, const ym::vec3f* pos) {
    return _compute_moments(
        ntriangles, (const ym::vec3i*)triangles, nverts, (const ym::vec3f*)pos);
}

//
// Public API.
//
std::tuple<float, ym::vec3f, ym::mat3f> compute_moments(
    int ntetra, const ym::vec4f* tetra, int nverts, const ym::vec3f* pos) {
    return _compute_moments(
        ntetra, (const ym::vec4i*)tetra, nverts, (const ym::vec3f*)pos);
}

//
// Compute collisions.
//
static void compute_collision(const scene* scn, const ym::vec2i& sids,
    std::vector<collision>* collisions) {
    auto bdy1 = scn->bodies[sids.x];
    auto bdy2 = scn->bodies[sids.y];
    for (auto vid = 0; vid < bdy2->shp->nverts; vid++) {
        auto p2 = transform_point(bdy2->frame, bdy2->shp->pos[vid]);
        auto overlap = scn->overlap_shape(sids.x, p2, scn->overlap_max_radius);
        if (!overlap) continue;
        auto triangle = bdy1->shp->triangles[overlap.eid];
        auto v0 = bdy1->shp->pos[triangle.x], v1 = bdy1->shp->pos[triangle.y],
             v2 = bdy1->shp->pos[triangle.z];
        auto p1 = transform_point(bdy1->frame, blerp(v0, v1, v2, overlap.euv));
        auto n1 = transform_direction(bdy1->frame, triangle_normal(v0, v1, v2));
        auto in = dot(n1, normalize(p2 - p1));
        if (in > -0.01f) continue;
        // if (dot(n1, normalize(p2 - p1)) > -0.01f) continue;
        auto col = collision();
        col.bdy1 = bdy1;
        col.bdy2 = bdy2;
        col.depth = overlap.dist;
        col.frame = ym::make_frame3_fromz(p2, n1);
        collisions->push_back(col);
    }
}

static void compute_collisions(
    scene* scene, std::vector<collision>* collisions) {
    // check which shapes might overlap
    auto body_collisions = std::vector<ym::vec2i>();
    scene->overlap_shapes(&body_collisions);
    // test all pair-wise objects
    collisions->clear();
    for (auto& sc : body_collisions) {
        auto bd1 = scene->bodies[sc.x], bd2 = scene->bodies[sc.y];
        if (!bd1->simulated && !bd2->simulated) continue;
        if (!bd1->shp->triangles) continue;
        if (!bd2->shp->triangles) continue;
        compute_collision(scene, sc, collisions);
        compute_collision(scene, {sc.y, sc.x}, collisions);
    }
}

//
// Apply an impulse where the position is relative to the center of mass.
//
static inline void apply_rel_impulse(
    rigid_body* bdy, const ym::vec3f impulse, const ym::vec3f rel_pos) {
    if (!bdy->simulated) return;
    bdy->lin_vel += impulse * bdy->_mass_inv;
    bdy->ang_vel += bdy->_inertia_inv_world * ym::cross(rel_pos, impulse);
}

//
// Solve constraints with PGS.
//
void solve_constraints(scene* scn, std::vector<collision>& collisions,
    const simulation_params& params) {
    // initialize computation
    for (auto& col : collisions) {
        col.local_impulse = ym::zero3f;
        col.impulse = ym::zero3f;
        col.r1 = col.frame.o - col.bdy1->_centroid_world,
        col.r2 = col.frame.o - col.bdy2->_centroid_world;
        col.meff_inv.z =
            1 /
            (col.bdy1->_mass_inv + col.bdy2->_mass_inv +
                dot(cross(col.r1, col.frame.z),
                    col.bdy1->_inertia_inv_world * cross(col.r1, col.frame.z)) +
                dot(cross(col.r2, col.frame.z),
                    col.bdy2->_inertia_inv_world * cross(col.r2, col.frame.z)));
        col.meff_inv.x =
            1 /
            (col.bdy1->_mass_inv + col.bdy2->_mass_inv +
                dot(cross(col.r1, col.frame.x),
                    col.bdy1->_inertia_inv_world * cross(col.r1, col.frame.x)) +
                dot(cross(col.r2, col.frame.x),
                    col.bdy2->_inertia_inv_world * cross(col.r2, col.frame.x)));
        col.meff_inv.y =
            1 /
            (col.bdy1->_mass_inv + col.bdy2->_mass_inv +
                dot(cross(col.r1, col.frame.y),
                    col.bdy1->_inertia_inv_world * cross(col.r1, col.frame.y)) +
                dot(cross(col.r2, col.frame.y),
                    col.bdy2->_inertia_inv_world * cross(col.r2, col.frame.y)));
    }

    // solve constraints
    for (int i = 0; i < params.solver_iterations; i++) {
        for (auto& col : collisions) {
            auto v1 = col.bdy1->lin_vel + cross(col.bdy1->ang_vel, col.r1),
                 v2 = col.bdy2->lin_vel + cross(col.bdy2->ang_vel, col.r2);
            auto vr = v2 - v1;
            apply_rel_impulse(col.bdy1, col.impulse, col.r1);
            apply_rel_impulse(col.bdy2, -col.impulse, col.r2);
            // auto offset = col.depth * 0.8f / params.dt;
            auto offset = 0.0f;
            auto local_impulse =
                col.meff_inv * transform_vector_inverse(
                                   col.frame, {-vr.x, -vr.y, -vr.z + offset});
            col.local_impulse += local_impulse;
            col.local_impulse.z = ym::clamp(
                col.local_impulse.z, 0.0f, std::numeric_limits<float>::max());
            col.local_impulse.x = ym::clamp(col.local_impulse.x,
                -col.local_impulse.z * 0.6f, col.local_impulse.z * 0.6f);
            col.local_impulse.y =
                ym::clamp(col.local_impulse.y, -col.local_impulse.z * 0.6f,
                    col.local_impulse.z - offset * 0.6f);
            col.impulse = transform_vector(col.frame, col.local_impulse);
            apply_rel_impulse(col.bdy1, -col.impulse, col.r1);
            apply_rel_impulse(col.bdy2, col.impulse, col.r2);
        }
    }
}

//
// Initialize the simulation
//
void init_simulation(scene* scn) {
    for (auto shp : scn->shapes) {
        std::tie(shp->_volume, shp->_centroid_local, shp->_inertia_local) =
            ysym::compute_moments(
                shp->nelems, shp->triangles, shp->nverts, shp->pos);
        shp->_inertia_inv_local = ym::inverse(shp->_inertia_local);
    }

    for (auto bdy : scn->bodies) {
        if (bdy->simulated) {
            bdy->_mass = bdy->mat->density * bdy->shp->_volume;
            bdy->_centroid_world =
                transform_point(bdy->frame, bdy->shp->_centroid_local);
            bdy->_mass_inv = 1 / bdy->_mass;
        } else {
            bdy->_mass = 0;
            bdy->_mass_inv = 0;
        }
    }
}

//
// Check function for numerical problems
//
inline bool isfinite(const ym::vec3f& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

//
// Advance simulation. Public API, see above.
//
void advance_simulation(scene* scn, const simulation_params& params) {
    // update centroid and inertia
    for (auto bdy : scn->bodies) {
        if (!bdy->simulated) continue;
        bdy->_centroid_world =
            transform_point(bdy->frame, bdy->shp->_centroid_local);
        bdy->_inertia_inv_world = ym::rot(bdy->frame) *
                                  bdy->shp->_inertia_inv_local *
                                  ym::transpose(ym::rot(bdy->frame));
    }

    // compute collisions
    auto collisions = std::vector<collision>();
    compute_collisions(scn, &collisions);

    // apply external forces
    ym::vec3f gravity_impulse = ym::vec3f(params.gravity) * params.dt;
    for (auto bdy : scn->bodies) {
        if (!bdy->simulated) continue;
        bdy->lin_vel += gravity_impulse;
    }

    // solve constraints
    solve_constraints(scn, collisions, params);

    // copy for visualization
    scn->__collisions = collisions;

    // apply drag
    for (auto bdy : scn->bodies) {
        if (!bdy->simulated) continue;
        bdy->lin_vel *= 1 - params.lin_drag;
        bdy->ang_vel *= 1 - params.ang_drag;
    }

    // update position and velocity
    for (auto bdy : scn->bodies) {
        if (!bdy->simulated) continue;

        // check for nans
        if (!isfinite(ym::pos(bdy->frame))) printf("nan detected\n");
        if (!isfinite(bdy->lin_vel)) printf("nan detected\n");
        if (!isfinite(bdy->ang_vel)) printf("nan detected\n");

        // translate the frame to the centroid
        auto centroid = ym::rot(bdy->frame) * bdy->shp->_centroid_local +
                        ym::pos(bdy->frame);
        // update centroid
        centroid += bdy->lin_vel * params.dt;
        float angle = ym::length(bdy->ang_vel) * params.dt;
        if (angle) {
            ym::vec3f axis = ym::normalize(bdy->ang_vel);
            ym::rot(bdy->frame) =
                ym::rotation_mat3(axis, angle) * ym::rot(bdy->frame);
            // TODO: if using matrices, I gotta orthonormalize them
        }
        // translate the frame back
        ym::pos(bdy->frame) =
            centroid - ym::rot(bdy->frame) * bdy->shp->_centroid_local;
    }

    // update acceleartion for collisions
    scn->overlap_refit(scn, (int)scn->shapes.size());
}

}  // namespace ysym
