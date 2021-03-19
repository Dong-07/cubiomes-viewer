#include "search.h"
#include "seedtables.h"
#include "mainwindow.h"

#include <QThread>

#include <algorithm>

extern MainWindow *gMainWindowInstance;


int check(int64_t s48, void *data)
{
    const StructureConfig sconf = *(const StructureConfig*) data;
    return isQuadBase(sconf, s48 - sconf.salt, 128);
}

/* Loads a seed list for a filter type from disk, or generates it if neccessary.
 * @mc      mincreaft version
 * @ftyp    filter type
 * @qb      output seed base list
 * @qbn     output length of seed base list
 * @dyn     list was dynamically allocated and requires a free
 * @sconf   structure configuration used for the bases
 */
static void genSeedBases(int mc, int ftyp, const int64_t **qb, int64_t *qbn,
                         int *dyn, StructureConfig *sconf)
{
    char fnam[128];

    const char *lbstr = NULL;
    const int64_t *lbset = NULL;
    int64_t lbcnt = 0;
    int64_t *dqb = NULL;

    *qb = NULL;
    *qbn = 0;
    *dyn = 0;

    switch (ftyp)
    {
    case F_QH_IDEAL:
        lbstr = "ideal";
        lbset = low20QuadIdeal;
        lbcnt = sizeof(low20QuadIdeal) / sizeof(int64_t);
        goto L_QH_ANY;
    case F_QH_CLASSIC:
        lbstr = "cassic";
        lbset = low20QuadClassic;
        lbcnt = sizeof(low20QuadClassic) / sizeof(int64_t);
        goto L_QH_ANY;
    case F_QH_NORMAL:
        lbstr = "normal";
        lbset = low20QuadHutNormal;
        lbcnt = sizeof(low20QuadHutNormal) / sizeof(int64_t);
        goto L_QH_ANY;
    case F_QH_BARELY:
        lbstr = "barely";
        lbset = low20QuadHutBarely;
        lbcnt = sizeof(low20QuadHutBarely) / sizeof(int64_t);
        goto L_QH_ANY;
L_QH_ANY:
        snprintf(fnam, sizeof(fnam), "protobases/quad_%s.txt", lbstr);
        *sconf = mc <= MC_1_12 ? SWAMP_HUT_CONFIG_112 : SWAMP_HUT_CONFIG;

        if ((dqb = loadSavedSeeds(fnam, qbn)) == NULL)
        {
            QMetaObject::invokeMethod(gMainWindowInstance, "openProtobaseMsg", Qt::QueuedConnection, Q_ARG(QString, QString(fnam)));

            int threads = QThread::idealThreadCount();
            int err = searchAll48(&dqb, qbn, fnam, threads, lbset, lbcnt, 20, check, sconf);

            QMetaObject::invokeMethod(gMainWindowInstance, "closeProtobaseMsg", Qt::BlockingQueuedConnection);

            if (err)
            {
                QMetaObject::invokeMethod(
                        gMainWindowInstance, "warning", Qt::BlockingQueuedConnection,
                        Q_ARG(QString, QString("Warning")),
                        Q_ARG(QString, QString("Failed to generate protobases.")));
                return;
            }
        }
        if (dqb)
        {
            // convert protobases to proper bases by subtracting the salt
            for (int64_t i = 0; i < (*qbn); i++)
                dqb[i] -= sconf->salt;
            *qb = (const int64_t*)dqb;
            *dyn = 1;
        }
        break;

    case F_QM_95:
        *qb = g_qm_95;
        *qbn = sizeof(g_qm_95) / sizeof(int64_t);
        *sconf = MONUMENT_CONFIG;
        *dyn = 0;
        break;
    case F_QM_90:
        *qb = g_qm_90;
        *qbn = sizeof(g_qm_90) / sizeof(int64_t);
        *sconf = MONUMENT_CONFIG;
        *dyn = 0;
        break;
    }
}


/* Produces a list of seed bases from precomputed lists, provided all candidates fit into a buffer.
 *
 * @param list      output list
 * @param mc        mincraft version
 * @param cond      conditions
 * @param ccnt      number of conditions
 * @param bufmax    maximum allowed buffer size
 */
void getCandidates(std::vector<int64_t>& list, int mc, const Condition *cond, int ccnt, int64_t bufmax)
{
    int ci;

    for (ci = 0; ci < ccnt; ci++)
    {
        if (cond[ci].relative)
            continue;

        int64_t q, qbn = 0, *p;
        const int64_t *qb = NULL;
        StructureConfig sconf;
        int dyn;
        int i, j;

        genSeedBases(mc, cond[ci].type, &qb, &qbn, &dyn, &sconf);
        if (!qb)
            continue;

        int x = cond[ci].x1;
        int z = cond[ci].z1;
        int w = cond[ci].x2 - x + 1;
        int h = cond[ci].z2 - z + 1;

        // does the set of candidates for this condition fit in memory?
        if (qbn * w*h * 4 * (int64_t)sizeof(int64_t) >= bufmax)
            goto L_next_cond;

        try {
            list.resize(qbn * w*h);
        } catch (...) {
            goto L_next_cond;
        }

        p = list.data();
        for (j = 0; j < h; j++)
        {
            for (i = 0; i < w; i++)
            {
                for (q = 0; q < qbn; q++)
                {
                    *p++ = moveStructure(qb[q], x+i, z+j);
                }
            }
        }

    L_next_cond:
        if (dyn && qb)
            free((void*)qb);

        if (!list.empty())
            break;
    }

    if (!list.empty())
    {
        std::sort(list.begin(), list.end());
        auto last = std::unique(list.begin(), list.end());
        list.erase(last, list.end());
    }
}


static bool intersectLineLine(double ax1, double az1, double ax2, double az2, double bx1, double bz1, double bx2, double bz2)
{
    double ax = ax2 - ax1, az = az2 - az1;
    double bx = bx2 - bx1, bz = bz2 - bz1;
    double adotb = ax * bz - az * bx;
    if (adotb == 0)
        return false; // parallel

    double cx = bx1 - ax1, cz = bz1 - az1;
    double t;
    t = (cx * az - cz * ax) / adotb;
    if (t < 0 || t > 1)
        return false;
    t = (cx * bz - cz * bx) / adotb;
    if (t < 0 || t > 1)
        return false;

    return true;
}

// does the line segment l1->l2 intersect the rectangle r1->r2
static bool intersectRectLine(double rx1, double rz1, double rx2, double rz2, double lx1, double lz1, double lx2, double lz2)
{
    if (lx1 >= rx1 && lx1 <= rx2 && lz1 >= rz1 && lz1 <= rz2) return true;
    if (lx2 >= rx1 && lx2 <= rx2 && lz2 >= rz1 && lz2 <= rz2) return true;
    if (intersectLineLine(lx1, lz1, lx2, lz2, rx1, rz1, rx1, rz2)) return true;
    if (intersectLineLine(lx1, lz1, lx2, lz2, rx1, rz2, rx2, rz2)) return true;
    if (intersectLineLine(lx1, lz1, lx2, lz2, rx2, rz2, rx2, rz1)) return true;
    if (intersectLineLine(lx1, lz1, lx2, lz2, rx2, rz1, rx1, rz1)) return true;
    return false;
}

static bool isInnerRingOk(int mc, int64_t seed, int x1, int z1, int x2, int z2, int r1, int r2)
{
    StrongholdIter sh;
    Pos p = initFirstStronghold(&sh, mc, seed);

    if (p.x >= x1 && p.x <= x2 && p.z >= z1 && p.z <= z2)
        return true;
    // Do a ray cast analysis, checking if any of the generation angles intersect the area.
    double c, s;
    c = cos(sh.angle + M_PI*2/3);
    s = sin(sh.angle + M_PI*2/3);
    if (intersectRectLine(x1-112, z1-112, x2+112, z2+112, c*r1, s*r1, c*r2, s*r2))
        return true;
    c = cos(sh.angle + M_PI*4/3);
    s = sin(sh.angle + M_PI*4/3);
    if (intersectRectLine(x1-112, z1-112, x2+112, z2+112, c*r1, s*r1, c*r2, s*r2))
        return true;

    return false;
}

int testCond(StructPos *spos, int64_t seed, const Condition *cond, int mc, LayerStack *g, std::atomic_bool *abort)
{
    int x1, x2, z1, z2;
    int rx1, rx2, rz1, rz2, rx, rz;
    Pos pc;
    StructureConfig sconf;
    int qual, valid;
    int xt, zt;
    int64_t s, r, rmin, rmax;
    Pos p[128];

    StructPos *sout = spos + cond->save;

    switch (cond->type)
    {
    case F_QH_IDEAL:
    case F_QH_CLASSIC:
    case F_QH_NORMAL:
    case F_QH_BARELY:
        sconf = mc <= MC_1_12 ? SWAMP_HUT_CONFIG_112 : SWAMP_HUT_CONFIG;
        qual = cond->type;

        if (cond->relative)
        {
            rx1 = ((cond->x1 << 9) + spos[cond->relative].cx) >> 9;
            rz1 = ((cond->z1 << 9) + spos[cond->relative].cz) >> 9;
            rx2 = ((cond->x2 << 9) + spos[cond->relative].cx) >> 9;
            rz2 = ((cond->z2 << 9) + spos[cond->relative].cz) >> 9;
        }
        else
        {
            rx1 = cond->x1;
            rz1 = cond->z1;
            rx2 = cond->x2;
            rz2 = cond->z2;
        }
        if (scanForQuads(
                sconf, 128, (seed) & MASK48, low20QuadHutBarely,
                sizeof(low20QuadHutBarely) / sizeof(int64_t), 20, sconf.salt,
                rx1, rz1, rx2 - rx1 + 1, rz2 - rz1 + 1, &pc, 1) >= 1)
        {
            rx = pc.x; rz = pc.z;
            s = moveStructure(seed, -rx, -rz);
            if ( U(qhutQual((s + sconf.salt) & 0xfffff) >= qual) &&
                 U(isQuadBaseFeature24(sconf, s, 7,7,9)) )
            {
                sout->sconf = sconf;
                getStructurePos(sconf.structType, mc, seed, rx+0, rz+0, p+0);
                getStructurePos(sconf.structType, mc, seed, rx+0, rz+1, p+1);
                getStructurePos(sconf.structType, mc, seed, rx+1, rz+0, p+2);
                getStructurePos(sconf.structType, mc, seed, rx+1, rz+1, p+3);
                pc = getOptimalAfk(p, 7,7,9, 0);
                sout->cx = pc.x;
                sout->cz = pc.z;
                return 1;
            }
        }
        return 0;

    case F_QM_95:   qual = 58*58*4 * 95 / 100;  goto L_qm_any;
    case F_QM_90:   qual = 58*58*4 * 90 / 100;
L_qm_any:
        sconf = MONUMENT_CONFIG;

        if (cond->relative)
        {
            rx1 = ((cond->x1 << 9) + spos[cond->relative].cx) >> 9;
            rz1 = ((cond->z1 << 9) + spos[cond->relative].cz) >> 9;
            rx2 = ((cond->x2 << 9) + spos[cond->relative].cx) >> 9;
            rz2 = ((cond->z2 << 9) + spos[cond->relative].cz) >> 9;
        }
        else
        {
            rx1 = cond->x1;
            rz1 = cond->z1;
            rx2 = cond->x2;
            rz2 = cond->z2;
        }
        if (scanForQuads(
                sconf, 160, (seed) & MASK48, g_qm_90,
                sizeof(g_qm_90) / sizeof(int64_t), 48,
                0, // 0 for salt offset as g_qm_90 are not protobases
                rx1, rz1, rx2 - rx1 + 1, rz2 - rz1 + 1, &pc, 1) >= 1)
        {
            rx = pc.x; rz = pc.z;
            s = moveStructure(seed, -rx, -rz);
            if (qmonumentQual(s) >= qual)
            {
                sout->sconf = sconf;
                getStructurePos(sconf.structType, mc, seed, rx+0, rz+0, p+0);
                getStructurePos(sconf.structType, mc, seed, rx+0, rz+1, p+1);
                getStructurePos(sconf.structType, mc, seed, rx+1, rz+0, p+2);
                getStructurePos(sconf.structType, mc, seed, rx+1, rz+1, p+3);
                pc = getOptimalAfk(p, 58,23,58, 0);
                sout->cx = pc.x - 29; // monument is centered
                sout->cz = pc.z - 29;
                return 1;
            }
        }
        return 0;


    case F_DESERT:
        sconf = mc <= MC_1_12 ? DESERT_PYRAMID_CONFIG_112 : DESERT_PYRAMID_CONFIG;
        goto L_struct_any;
    case F_HUT:
        sconf = mc <= MC_1_12 ? SWAMP_HUT_CONFIG_112 : SWAMP_HUT_CONFIG;
        goto L_struct_any;
    case F_JUNGLE:
        sconf = mc <= MC_1_12 ? JUNGLE_PYRAMID_CONFIG_112 : JUNGLE_PYRAMID_CONFIG;
        goto L_struct_any;
    case F_IGLOO:
        sconf = mc <= MC_1_12 ? IGLOO_CONFIG_112 : IGLOO_CONFIG;
        goto L_struct_any;
    case F_MONUMENT:    sconf = MONUMENT_CONFIG;        goto L_struct_any;
    case F_VILLAGE:     sconf = VILLAGE_CONFIG;         goto L_struct_any;
    case F_OUTPOST:     sconf = OUTPOST_CONFIG;         goto L_struct_any;
    case F_MANSION:     sconf = MANSION_CONFIG;         goto L_struct_any;
    case F_PORTAL:      sconf = RUINED_PORTAL_CONFIG;   goto L_struct_any;

L_struct_any:
        x1 = cond->x1;
        z1 = cond->z1;
        x2 = cond->x2;
        z2 = cond->z2;
        if (cond->relative)
        {
            x1 += spos[cond->relative].cx;
            z1 += spos[cond->relative].cz;
            x2 += spos[cond->relative].cx;
            z2 += spos[cond->relative].cz;
        }

        if (sconf.regionSize == 32)
        {
            rx1 = x1 >> 9;
            rz1 = z1 >> 9;
            rx2 = x2 >> 9;
            rz2 = z2 >> 9;
        }
        else
        {
            rx1 = (x1 / (sconf.regionSize << 4)) - (x1 < 0);
            rz1 = (z1 / (sconf.regionSize << 4)) - (z1 < 0);
            rx2 = (x2 / (sconf.regionSize << 4)) - (x2 < 0);
            rz2 = (z2 / (sconf.regionSize << 4)) - (z2 < 0);
        }

        // TODO: warn if multistructure clusters are used as a positional
        // dependency (the centre can change based on biomes)

        sout->cx = xt = 0;
        sout->cz = zt = 0;
        qual = 0;

        // Note "<="
        for (rz = rz1; rz <= rz2 && !*abort; rz++)
        {
            for (rx = rx1; rx <= rx2; rx++)
            {
                if (!getStructurePos(sconf.structType, mc, seed, rx+0, rz+0, &pc))
                    continue;
                if (pc.x >= x1 && pc.x <= x2 && pc.z >= z1 && pc.z <= z2)
                {
                    if (g && !isViableStructurePos(sconf.structType, mc, g, seed, pc.x, pc.z))
                        continue;

                    xt += pc.x;
                    zt += pc.z;

                    if (++qual >= cond->count)
                    {
                        sout->sconf = sconf;
                        sout->cx = xt / qual;
                        sout->cz = zt / qual;
                        return 1;
                    }
                }
            }
        }
        return 0;

    case F_TREASURE:
        sconf = TREASURE_CONFIG;
        x1 = cond->x1;
        z1 = cond->z1;
        x2 = cond->x2;
        z2 = cond->z2;
        if (cond->relative)
        {
            x1 += spos[cond->relative].cx;
            z1 += spos[cond->relative].cz;
            x2 += spos[cond->relative].cx;
            z2 += spos[cond->relative].cz;
        }
        rx1 = x1 >> 4;
        rz1 = z1 >> 4;
        rx2 = x2 >> 4;
        rz2 = z2 >> 4;

        sout->cx = xt = 0;
        sout->cz = zt = 0;
        qual = 0;

        for (int rz = rz1; rz <= rz2; rz++)
        {
            for (int rx = rx1; rx <= rx2; rx++)
            {
                pc = { (rx << 4) + 9, (rz << 4) + 9 };
                if (pc.x < x1 || pc.x > x2 || pc.z < z1 || pc.z > z2)
                    continue;
                if (!isTreasureChunk(seed, rx, rz))
                    continue;
                if (g && !isViableStructurePos(sconf.structType, mc, g, seed, pc.x, pc.z))
                    continue;

                xt += pc.x;
                zt += pc.z;

                if (++qual >= cond->count)
                {
                    sout->sconf = sconf;
                    sout->cx = xt / qual;
                    sout->cz = zt / qual;
                    return 1;
                }
            }
        }
        return 0;

    case F_SPAWN:
        // TODO: warn if spawn is used for relative positioning
        sout->cx = 0;
        sout->cz = 0;
        if (!g)
            return 1;

        x1 = cond->x1;
        z1 = cond->z1;
        x2 = cond->x2;
        z2 = cond->z2;
        if (cond->relative)
        {
            x1 += spos[cond->relative].cx;
            z1 += spos[cond->relative].cz;
            x2 += spos[cond->relative].cx;
            z2 += spos[cond->relative].cz;
        }
        applySeed(g, seed);
        if (*abort) return 0;
        pc = getSpawn(mc, g, NULL, seed);
        if (pc.x >= x1 && pc.x <= x2 && pc.z >= z1 && pc.z <= z2)
        {
            sout->cx = pc.x;
            sout->cz = pc.z;
            return 1;
        }
        return 0;

    case F_STRONGHOLD:
        x1 = cond->x1;
        z1 = cond->z1;
        x2 = cond->x2;
        z2 = cond->z2;
        if (cond->relative)
        {
            x1 += spos[cond->relative].cx;
            z1 += spos[cond->relative].cz;
            x2 += spos[cond->relative].cx;
            z2 += spos[cond->relative].cz;
        }
        // TODO: add option for looking for the first stronghold only (which would be faster)

        rx1 = abs(x1); rx2 = abs(x2);
        rz1 = abs(z1); rz2 = abs(z2);
        if (x1 <= 112 && x2 >= -112 && z1 <= 112 && z2 >= -112)
        {
            rmin = 0;
        }
        else
        {
            xt = (rx1 < rx2 ? rx1 : rx2) - 112;
            zt = (rz1 < rz2 ? rz1 : rz2) - 112;
            rmin = xt*xt + zt*zt;
        }
        xt = (rx1 > rx2 ? rx1 : rx2) + 112;
        zt = (rz1 > rz2 ? rz1 : rz2) + 112;
        rmax = xt*xt + zt*zt;

        // -MC_1_8 formula:
        // r = 640 + [0,1]*512 (+/-112)
        // MC_1_9+ formula:
        // r = 1408 + 3072*n + 1280*[0,1] (+/-112)

        if (mc < MC_1_9)
        {
            if (rmax < 640*640 || rmin > 1152*1152)
                return 0;
            r = 0;
            rmin = 640;
            rmax = 1152;
        }
        else
        {   // check if the area is entirely outside the radii ranges in which strongholds can generate
            if (rmax < 1408*1408)
                return 0;
            rmin = sqrt(rmin);
            rmax = sqrt(rmax);
            r = (rmax - 1408) / 3072;       // maximum relevant ring number
            if (rmax - rmin < 3072-1280)    // area does not span more than one ring
            {
                if (rmin > 1408+1280+3072*r)
                    return 0;               // area is between rings
            }
            rmin = 1408;
            rmax = 1408+1280;
        }
        // if we are only looking at the inner ring, we can check if the generation angles are suitable
        if (r == 0 && !isInnerRingOk(mc, seed, x1, z1, x2, z2, rmin, rmax))
            return 0;

        // pre-biome-checks complete, the area appears to line up with possible generation positions
        if (!g)
        {
            // TODO: warn if strongholds are used for relative positioning
            sout->cx = 0;
            sout->cz = 0;
            return 1;
        }
        else
        {
            StrongholdIter sh;
            initFirstStronghold(&sh, mc, seed);
            applySeed(g, seed);
            qual = 0;
            while (nextStronghold(&sh, g, NULL) > 0)
            {
                if (*abort || sh.ringnum > r)
                    break;

                if (sh.pos.x >= x1 && sh.pos.x <= x2 && sh.pos.z >= z1 && sh.pos.z <= z2)
                {
                    if (++qual >= cond->count)
                    {
                        sout->cx = sh.pos.x;
                        sout->cz = sh.pos.z;
                        return 1;
                    }
                }

                if (sh.ringnum == r && sh.ringidx+1 == sh.ringmax)
                    break;
            }
        }
        return 0;

    case F_SLIME:
        if (cond->relative)
        {
            rx1 = ((cond->x1 << 4) + spos[cond->relative].cx) >> 4;
            rz1 = ((cond->z1 << 4) + spos[cond->relative].cz) >> 4;
            rx2 = ((cond->x2 << 4) + spos[cond->relative].cx) >> 4;
            rz2 = ((cond->z2 << 4) + spos[cond->relative].cz) >> 4;
        }
        else
        {
            rx1 = cond->x1;
            rz1 = cond->z1;
            rx2 = cond->x2;
            rz2 = cond->z2;
        }
        qual = 0;
        for (int rz = rz1; rz <= rz2; rz++)
        {
            for (int rx = rx1; rx <= rx2; rx++)
            {
                if (isSlimeChunk(seed, rx, rz))
                    if (++qual >= cond->count)
                        return 1;
            }
        }
        return 0;

    case F_BIOME:           s = 0; qual = L_VORONOI_ZOOM_1;     goto L_biome_filter_any;
    case F_BIOME_4_RIVER:   s = 2; qual = L_RIVER_MIX_4;        goto L_biome_filter_any;
    case F_BIOME_16_SHORE:  s = 4; qual = L_SHORE_16;           goto L_biome_filter_any;
    case F_BIOME_64_RARE:   s = 6; qual = L_RARE_BIOME_64;      goto L_biome_filter_any;
    case F_BIOME_256_BIOME: s = 8; qual = L_BIOME_256;          goto L_biome_filter_any;
    case F_BIOME_256_OTEMP: s = 8; qual = L13_OCEAN_TEMP_256;   goto L_biome_filter_any;

L_biome_filter_any:
        if (cond->relative)
        {
            rx1 = ((cond->x1 << s) + spos[cond->relative].cx) >> s;
            rz1 = ((cond->z1 << s) + spos[cond->relative].cz) >> s;
            rx2 = ((cond->x2 << s) + spos[cond->relative].cx) >> s;
            rz2 = ((cond->z2 << s) + spos[cond->relative].cz) >> s;
        }
        else
        {
            rx1 = cond->x1;
            rz1 = cond->z1;
            rx2 = cond->x2;
            rz2 = cond->z2;
        }
        sout->cx = ((rx1 + rx2) << s) >> 1;
        sout->cz = ((rz1 + rz2) << s) >> 1;
        if (!g)
        {
            if (qual != L13_OCEAN_TEMP_256)
                return 1;
            if (mc < MC_1_13)
                return 0;
            thread_local LayerStack g_otemp;
            if (!g_otemp.entry_1)
                setupGenerator(&g_otemp, MC_1_13);
            g = &g_otemp;
        }
        valid = 0;
        if (rx2 >= rx1 || rz2 >= rz1 || !*abort)
        {
            int w = rx2-rx1+1;
            int h = rz2-rz1+1;
            int *area = allocCache(&g->layers[qual], w, h);
            if (checkForBiomes(g, qual, area, seed, rx1, rz1, w, h, cond->bfilter, 0) > 0)
            {
                // check biome exclusion
                uint64_t b = 0, bm = 0;
                for (int i = 0; i < w*h; i++)
                {
                    int id = area[i];
                    if (id < 128) b |= (1ULL << id);
                    else bm |= (1ULL << (id-128));
                }
                if ((b & cond->exclb) == 0 && (bm & cond->exclm) == 0)
                    valid = 1;
            }
            free(area);
        }
        return valid;


    case F_TEMPS:
        if (cond->relative)
        {
            rx1 = ((cond->x1 << 10) + spos[cond->relative].cx) >> 10;
            rz1 = ((cond->z1 << 10) + spos[cond->relative].cz) >> 10;
            rx2 = ((cond->x2 << 10) + spos[cond->relative].cx) >> 10;
            rz2 = ((cond->z2 << 10) + spos[cond->relative].cz) >> 10;
        }
        else
        {
            rx1 = cond->x1;
            rz1 = cond->z1;
            rx2 = cond->x2;
            rz2 = cond->z2;
        }
        sout->cx = ((rx1 + rx2) << 10) >> 1;
        sout->cz = ((rz1 + rz2) << 10) >> 1;
        if (!g) return 1;
        return checkForTemps(g, seed, rx1, rz1, rx2-rx1+1, rz2-rz1+1, cond->temps);

    default:
        break;
    }

    return 1;
}
