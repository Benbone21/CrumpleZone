// Body of the beam-geometry kernel, included once per instruction set (see simdkern.cpp).
// Wants KERN_NAME, KERN_ATTR and KERN_SQRT(v4d) -> v4d. Plain IEEE add/sub/mul/div/sqrt in a
// fixed order: never reassociate, never fuse.
KERN_ATTR static void KERN_NAME(const Beam* beams, int count, const Node* nodes, const BeamGeo& out) {
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        const Node* A[4] = {&nodes[beams[i].a], &nodes[beams[i + 1].a], &nodes[beams[i + 2].a], &nodes[beams[i + 3].a]};
        const Node* B[4] = {&nodes[beams[i].b], &nodes[beams[i + 1].b], &nodes[beams[i + 2].b], &nodes[beams[i + 3].b]};
        const v4d dx = v4d{B[0]->p.x, B[1]->p.x, B[2]->p.x, B[3]->p.x} - v4d{A[0]->p.x, A[1]->p.x, A[2]->p.x, A[3]->p.x};
        const v4d dy = v4d{B[0]->p.y, B[1]->p.y, B[2]->p.y, B[3]->p.y} - v4d{A[0]->p.y, A[1]->p.y, A[2]->p.y, A[3]->p.y};
        const v4d dz = v4d{B[0]->p.z, B[1]->p.z, B[2]->p.z, B[3]->p.z} - v4d{A[0]->p.z, A[1]->p.z, A[2]->p.z, A[3]->p.z};
        const v4d L = KERN_SQRT(dx * dx + dy * dy + dz * dz);
        const v4d ux = dx / L, uy = dy / L, uz = dz / L;
        const v4d wx = v4d{B[0]->v.x, B[1]->v.x, B[2]->v.x, B[3]->v.x} - v4d{A[0]->v.x, A[1]->v.x, A[2]->v.x, A[3]->v.x};
        const v4d wy = v4d{B[0]->v.y, B[1]->v.y, B[2]->v.y, B[3]->v.y} - v4d{A[0]->v.y, A[1]->v.y, A[2]->v.y, A[3]->v.y};
        const v4d wz = v4d{B[0]->v.z, B[1]->v.z, B[2]->v.z, B[3]->v.z} - v4d{A[0]->v.z, A[1]->v.z, A[2]->v.z, A[3]->v.z};
        const v4d vr = wx * ux + wy * uy + wz * uz;
        std::memcpy(out.L + i, &L, sizeof L);
        std::memcpy(out.ux + i, &ux, sizeof ux);
        std::memcpy(out.uy + i, &uy, sizeof uy);
        std::memcpy(out.uz + i, &uz, sizeof uz);
        std::memcpy(out.vrel + i, &vr, sizeof vr);
    }
    for (; i < count; i++) {  // the same operations, one beam at a time
        const Node& a = nodes[beams[i].a];
        const Node& b = nodes[beams[i].b];
        const double dx = b.p.x - a.p.x, dy = b.p.y - a.p.y, dz = b.p.z - a.p.z;
        const double L = __builtin_sqrt(dx * dx + dy * dy + dz * dz);
        const double ux = dx / L, uy = dy / L, uz = dz / L;
        out.L[i] = L, out.ux[i] = ux, out.uy[i] = uy, out.uz[i] = uz;
        out.vrel[i] = (b.v.x - a.v.x) * ux + (b.v.y - a.v.y) * uy + (b.v.z - a.v.z) * uz;
    }
}
