
 // QoS simulation core.
// Build: g++ -O2 -std=c++17 -o qos_engine qos_engine.cpp
// Usage:
// qos_engine link_mbps duration_s voice_calls video_streams bulk_downloads
//           gap_us video_quantum bulk_quantum [seed] [path_delay_ms]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <queue>
#include <random>
#include <utility>
#include <vector>

using namespace std;

enum { VOICE, VIDEO, BULK, NC };

static const char* NAME[NC] = {"Voice", "Video", "Bulk"};
static const int SIZE[NC] = {200, 1200, 1500};
static const double VOICE_PERIOD = 0.02;
static const double VIDEO_MBPS = 1.9;

struct Flow {
    int cls;
    double period, next;
};

struct Pkt {
    double t;
    int flow, cls, size;
    double proc;
};

// Merges all flows in time order without storing the packets.
struct Source {
    vector<Flow> f;
    double T;

    priority_queue<
        pair<double, int>,
        vector<pair<double, int> >,
        greater<pair<double, int> >
    > h;

    mt19937 rng{1234};
    exponential_distribution<double> ex{20000.0};

    Source(vector<Flow> fl, double dur)
        : f(move(fl)), T(dur) {
        for (int i = 0; i < (int)f.size(); i++) {
            h.push(make_pair(f[i].next, i));
        }
    }

    bool peek(double& t) {
        if (h.empty()) return false;
        t = h.top().first;
        return t < T;
    }

    Pkt pop() {
        // Compatible with older MinGW compilers.
        pair<double, int> top = h.top();
        double t = top.first;
        int i = top.second;

        h.pop();

        Pkt p{
            t,
            i,
            f[i].cls,
            SIZE[f[i].cls],
            50e-6 + ex(rng)
        };

        f[i].next += f[i].period;
        h.push(make_pair(f[i].next, i));

        return p;
    }
};

static vector<Flow> make_flows(
    int v,
    int vd,
    int b,
    double gap_us,
    unsigned seed
) {
    mt19937 rng(seed);
    vector<Flow> f;

    auto add = [&](int c, double period) {
        uniform_real_distribution<double> ph(0, period);
        f.push_back({c, period, ph(rng)});
    };

    for (int i = 0; i < v; i++) {
        add(VOICE, VOICE_PERIOD);
    }

    for (int i = 0; i < vd; i++) {
        add(VIDEO, SIZE[VIDEO] * 8.0 / (VIDEO_MBPS * 1e6));
    }

    for (int i = 0; i < b; i++) {
        add(BULK, gap_us * 1e-6);
    }

    return f;
}

struct Res {
    vector<float> d[NC];

    double sum[NC] = {};
    double bytes[NC] = {};
    double jsum[NC] = {};

    long jn[NC] = {};
    long sent[NC] = {};
    long lost[NC] = {};
    long tot[NC] = {};

    long policer = 0;
    long queue = 0;
    long ecn = 0;
};

static Res simulate(
    bool qos,
    const vector<Flow>& fl,
    double T,
    double bps,
    int vq,
    int bq,
    double prop
) {
    Res r;
    Source src(fl, T);

    mt19937 rng(7);
    uniform_real_distribution<double> U(0.0, 1.0);

    deque<Pkt> q[NC];

    // Same 600-packet total buffer in both simulations.
    const size_t cap[NC] = {100, 200, 300};
    const size_t fifo_cap = 600;

    const double frac[NC] = {0.10, 0.45, 0.80};
    const double burst[NC] = {3000, 30000, 64000};

    double tok[NC], rate[NC], lastT[NC] = {};

    for (int c = 0; c < NC; c++) {
        rate[c] = frac[c] * bps / 8.0;
        tok[c] = burst[c];
    }

    vector<double> lastD(fl.size(), -1.0);

    int cur = 0;
    bool fresh = true;

    double def[2] = {0.0, 0.0};
    const int quant[2] = {vq, bq};

    double t = 0.0, nt;

    auto admit = [&](const Pkt& p) {
        r.tot[p.cls]++;

        // Baseline FIFO with tail-drop.
        if (!qos) {
            if (q[0].size() >= fifo_cap) {
                r.lost[p.cls]++;
                r.queue++;
                return;
            }

            q[0].push_back(p);
            return;
        }

        int c = p.cls;

        // Token-bucket policer.
        tok[c] = min(
            burst[c],
            tok[c] + (p.t - lastT[c]) * rate[c]
        );

        lastT[c] = p.t;

        if (tok[c] < p.size) {
            r.lost[c]++;
            r.policer++;
            return;
        }

        tok[c] -= p.size;

        // Per-class tail-drop queue limits.
        if (q[c].size() >= cap[c]) {
            r.lost[c]++;
            r.queue++;
            return;
        }

        // RED/ECN marking for bulk traffic.
        if (c == BULK && q[c].size() > cap[c] / 3) {
            double pr =
                (double)(q[c].size() - cap[c] / 3) /
                (cap[c] * 2.0 / 3.0);

            if (U(rng) < pr) {
                r.ecn++;
            }
        }

        q[c].push_back(p);
    };

    for (;;) {
        bool empty = qos
            ? (q[VOICE].empty() &&
               q[VIDEO].empty() &&
               q[BULK].empty())
            : q[0].empty();

        if (empty) {
            if (!src.peek(nt)) break;
            t = max(t, nt);
        }

        while (src.peek(nt) && nt <= t) {
            admit(src.pop());
        }

        int c = -1;

        if (!qos) {
            if (!q[0].empty()) {
                c = 0;
            }
        } else if (!q[VOICE].empty()) {
            // Strict priority for voice traffic.
            c = VOICE;
        } else if (!q[VIDEO].empty() || !q[BULK].empty()) {
            // Deficit Round Robin for video and bulk.
            for (;;) {
                int k = cur + 1;

                if (q[k].empty()) {
                    def[cur] = 0.0;
                    cur ^= 1;
                    fresh = true;
                    continue;
                }

                if (fresh) {
                    def[cur] += quant[cur];
                    fresh = false;
                }

                if (q[k].front().size <= def[cur]) {
                    def[cur] -= q[k].front().size;
                    c = k;
                    break;
                }

                cur ^= 1;
                fresh = true;
            }
        }

        if (c < 0) continue;

        Pkt p = q[c].front();
        q[c].pop_front();

        // Packet serialization time.
        t += p.size * 8.0 / bps;

        // Queueing + serialization + path + processing delay.
        double d = t - p.t + prop + p.proc;

        r.sent[p.cls]++;
        r.sum[p.cls] += d;
        r.d[p.cls].push_back((float)(d * 1000.0));

        if (t <= T) {
            r.bytes[p.cls] += p.size;
        }

        // Per-flow jitter.
        if (lastD[p.flow] >= 0.0) {
            r.jsum[p.cls] += fabs(d - lastD[p.flow]);
            r.jn[p.cls]++;
        }

        lastD[p.flow] = d;
    }

    return r;
}

static void emit(const char* key, Res& r, double T) {
    printf("\"%s\":{\"classes\":[", key);

    for (int c = 0; c < NC; c++) {
        auto& d = r.d[c];
        double p99 = 0.0;

        if (!d.empty()) {
            size_t k = (size_t)(0.99 * (d.size() - 1));

            nth_element(
                d.begin(),
                d.begin() + k,
                d.end()
            );

            p99 = d[k];
        }

        printf(
            "%s{\"name\":\"%s\",\"total\":%ld,"
            "\"sent\":%ld,\"lost\":%ld,"
            "\"avg\":%.4f,\"p99\":%.4f,"
            "\"jitter\":%.4f,\"loss\":%.3f,"
            "\"thr\":%.3f}",

            c ? "," : "",
            NAME[c],
            r.tot[c],
            r.sent[c],
            r.lost[c],

            r.sent[c]
                ? r.sum[c] / r.sent[c] * 1000.0
                : 0.0,

            p99,

            r.jn[c]
                ? r.jsum[c] / r.jn[c] * 1000.0
                : 0.0,

            r.tot[c]
                ? 100.0 * r.lost[c] / r.tot[c]
                : 0.0,

            r.bytes[c] * 8.0 / T / 1e6
        );
    }

    printf(
        "],\"policer\":%ld,\"queue\":%ld,\"ecn\":%ld}",
        r.policer,
        r.queue,
        r.ecn
    );
}

int main(int argc, char** argv) {
    auto arg = [&](int i, double d) {
        return argc > i ? atof(argv[i]) : d;
    };

    double link = max(10.0, arg(1, 100.0));
    double T = max(0.1, min(arg(2, 1.0), 3.0));

    int v = (int)arg(3, 6);
    int vd = (int)arg(4, 3);
    int b = (int)arg(5, 8);

    double gap = max(20.0, arg(6, 50.0));

    int vq = (int)max(100.0, arg(7, 3000.0));
    int bq = (int)max(100.0, arg(8, 1500.0));

    unsigned seed = (unsigned)arg(9, 42.0);

    double prop =
        max(0.0, min(arg(10, 5.0), 100.0)) / 1000.0;

    vector<Flow> fl = make_flows(v, vd, b, gap, seed);

    double offered =
        v * 0.08 +
        vd * VIDEO_MBPS +
        b * 12000.0 / gap;

    Res a = simulate(
        false, fl, T, link * 1e6, vq, bq, prop
    );

    Res q = simulate(
        true, fl, T, link * 1e6, vq, bq, prop
    );

    printf(
        "{\"config\":{\"link\":%.0f,\"duration\":%.2f,"
        "\"offered\":%.2f,\"load_pct\":%.1f},",
        link,
        T,
        offered,
        100.0 * offered / link
    );

    emit("fifo", a, T);
    printf(",");
    emit("qos", q, T);
    printf("}\n");

    return 0;
}
