---
title: "Shed Load, Not Tears: Lessons from the GitHub Incident"
date: 2026-08-21
tags: ["openshift", "kubernetes", "reliability", "resilience", "github"]
summary: "What GitHub's August 17 outage teaches about load shedding on shared, multi-tenant OpenShift clusters."
---

On August 17, GitHub experienced a significant outage. The
[post-mortem][blog] the company published afterward reports a striking
figure: monthly commits grew from roughly 1.4 billion in April to 2.9
billion, close to a doubling in four months.

It is tempting to read this as a capacity problem, a platform that did not
provision ahead of its own growth. That reading is incomplete, and for
anyone responsible for production infrastructure it is a costly one. The
more useful question is not how much hardware GitHub had, but how its
systems behaved once demand exceeded what they were built for.

In what follows, we look at why demand of this kind is hard to provision
against on a shared cluster, why autoscaling is not a sufficient answer,
and how the primitives in OpenShift can be configured so that a
multi-tenant cluster protects its critical work and sheds the rest rather
than collapsing as a whole.

## What the incident involved

GitHub attributes the outage to capacity pressure rather than a code or
configuration change, but the way that pressure propagated, set out in the
[incident report][status], is the instructive part. An Istio sidecar
reached its concurrency limit, and the autoscaling policy meant to relieve
it was watching the host service rather than the constrained sidecar. The signal that mattered was
invisible to the loop that should have reacted to it, and the bottleneck
held.

From there it spread. Four HAProxy nodes exhausted their flow limits,
degrading the gateway authentication path and turning a local constraint
into widespread authentication failures. During recovery, a latent retry
bug in VS Code amplified traffic to one internal endpoint roughly tenfold,
and the Copilot Token Service saw its load climb from a normal 7,000 to
9,000 requests per second to between 70,000 and 100,000. Errors produced
retries, and retries produced more errors. GitHub's remediation list,
which names consistent retry limits, retry budgets, and variable timeouts,
reads as a direct response to that amplification.

One detail connects to what follows. The edge tier that gave way, HAProxy,
is the same technology behind the traditional OpenShift Router discussed
below, and the failure mode, a fixed limit reached under load, is the kind
of thing that has to be planned for rather than discovered.

## Background: demand without a human ceiling

For most of the last two decades, the systems we build have carried an
assumption that rarely appears in a design document: that a person sits at
the other end of most requests. People type, read, and pause to think.
That pace acts as an invisible rate limiter on commit volume, API traffic,
and CI activity alike.

Capacity plans have held up in part because of it. Demand had a natural
ceiling, and that ceiling was human.

That ceiling is now weakening. A growing share of activity originates from
software driven by other software, which does not tire or pause. The
demand curve has not only grown steeper; it has lost the mechanism that
used to bound it. This is not, fundamentally, a GitHub story. It applies
to most workloads that were sized against human-speed input.

This matters most on the clusters most enterprises actually run. A single
OpenShift cluster is typically multi-tenant, hosting business-critical
services alongside batch pipelines, internal tools, and experimentation,
all sharing the same nodes. When demand loses its human ceiling, the
question is no longer whether the cluster can serve everything at once, but
which tenants it continues to protect when it cannot.

## Autoscaling helps, but it is not the whole answer

On OpenShift, the natural response to rising load is to autoscale, and it
is a good one. The Horizontal Pod Autoscaler adds replicas, the Cluster
Autoscaler adds nodes, and for the demand it is designed to absorb,
gradual, sustained growth within the cluster's headroom, autoscaling does
exactly what it should. Any resilient cluster needs it, and the point here
is not to do less of it but to recognize what it does not cover.

Two gaps matter. The first is timescale. A spike can arrive in
milliseconds, an HPA reacts over tens of seconds, and a new node can take
minutes to arrive. Autoscaling closes the gap afterward, but during that
interval the pods already running absorb the load, so something else has
to protect them in the meantime.

The second is priority. Autoscaling works to keep every workload served,
which is the right default, but it does not by itself know which workloads
to favor when capacity cannot stretch to all of them. On a multi-tenant
cluster that decision is exactly the one that matters, and it has to be
supplied from outside the autoscaler.

Load shedding is the complement that fills both gaps. It holds the line in
the seconds before new capacity arrives, and it applies the priority
decision that autoscaling does not make. Seen this way, GitHub's incident
was less a shortage of capacity than the absence of that complement: under
extreme load, a well-designed system should decline its least important
work in order to keep its most important work running. Resilience depends
on both, on provisioning enough and on behaving well once provisioning is
no longer enough. OpenShift provides the primitives for the second part,
but only if they are configured.

## Step one: understand your workloads

Before any of the controls below can help, the cluster's operators have to
know what runs on it and how much each workload matters. On a
single-tenant cluster this is implicit. On a shared, multi-tenant cluster
it is not, and the gap is where most resilience efforts fail: a control
that cannot tell which tenant is critical cannot protect it.

The foundation is therefore an inventory of workloads and an agreed
criticality classification, decided with the business rather than inferred
from workload type. That classification should be the single source of
truth that drives everything downstream: the `PriorityClass` a workload
receives, the quota tier of its namespace, the tier its traffic carries at
the gateway, and the admission rules that apply to it under stress.

The classification is most useful when it is expressed as data and
enforced by automation rather than maintained by hand. A consistent set of
namespace and workload labels, for tenant and for criticality, can be
reconciled by GitOps tooling such as OpenShift GitOps, or by an operator,
so that priority classes, quotas, and policies are generated from the
classification and stay in step with it. Drift, where a workload's real
priority no longer matches its declared one, is a common source of
surprise during an incident.

This step is also where priority inversion is easiest to catch. A
classification is only meaningful if it is consistent across dependencies,
so the mechanism that assigns criticality should also check that no
critical workload depends on a less critical one. We return to that risk
below, but it belongs at the start: a classification built without it will
protect the wrong things.

## Shedding load on OpenShift

### Shed at the edge, by priority

The first line of defense is the ingress, because autoscaling refills
capacity behind that layer rather than replacing it. The traditional
OpenShift ingress is the Router, built on HAProxy and configured through
`Route` objects. It can enforce per-route connection and rate limits
through route annotations, but the controls are coarse: the standard
limits key on source IP, which behaves poorly behind NAT or shared
proxies, and separate router replicas do not coordinate every limit
between them. It is worth remembering that HAProxy, the same technology,
is what exhausted its flow limits at GitHub's edge.

Gateway API is the newer path, and it improves on this. On OpenShift it is
implemented with Istio and an Envoy data plane, managed by the Ingress
Operator, and that support has matured across recent releases. Because the
gateway runs on Envoy, the shedding controls otherwise found inside the
mesh become available at the edge. A circuit breaker that caps pending
requests, for example through `http1MaxPendingRequests`, bounds the queue
and lets an overload surface as an immediate `503` rather than a slow
cascade of timeouts, and outlier detection ejects hosts that begin to
fail. The `HTTPRoute` model also makes tier-based routing explicit, so
traffic can be matched and shed by business priority at the gateway rather
than through annotations.

Two caveats are worth noting. The core Gateway API specification does not
yet standardize rate limiting or circuit breaking, so on OpenShift these
come from the underlying Istio and Envoy configuration rather than from
portable Gateway API objects. And rate limiting has two forms: a local
limit is enforced per Envoy pod and works as a load-shedding tool, while a
shared, cluster-wide limit requires a global rate-limit service backed by
Redis, which is more accurate but adds a dependency in the request path.

The same Envoy controls apply east-west inside Red Hat OpenShift Service
Mesh, so a `DestinationRule` can circuit-break traffic between services as
well as at the edge. Shedding should be priority-aware, with the priority
coming from the business rather than a guess about workload type. Where
possible, the rejection should happen at the edge, where it is cheap,
rather than deep in the stack after a request has already taken a database
connection.

### Reserve capacity by business priority, not workload type

Teams often assume that a workload's type settles its importance. It does
not. Batch is not a synonym for unimportant: an overnight settlement run,
a regulatory report with a deadline, or a fraud-scoring job may matter
more than an interactive dashboard.

Importance is a business classification, expressed on the platform as a
`PriorityClass`. The shedding machinery should act on that classification
rather than on whether the work is interactive or batch.

The native objects then enforce it. A `ResourceQuota` scoped to a set of
priority classes, using a `scopeSelector` on `PriorityClass`, caps the
lower tiers at admission time; once a tier exhausts its quota, the API
server declines to create more of its pods. Preemption complements this,
letting critical pods evict lower-priority ones under node pressure, while
a lower tier set to `preemptionPolicy: Never` waits instead of displacing
others. A small pool of overprovisioning "pause" pods can hold reserved
capacity that critical work preempts quickly, closing the timing gap that
autoscaling leaves open.

### Audit for priority inversions

A priority classification is only as strong as its weakest dependency. A
customer-facing payment path may be marked critical while a service it
depends on, a token issuer, a shared cache, or a database proxy, is marked
ordinary. During an incident, the critical path then fails because the
platform has shed or preempted something it silently relied on. This is a
priority inversion.

These are worth auditing for in advance. The service topology that Kiali
renders from mesh telemetry shows which services actually call which, and
any call from a higher-priority service to a lower-priority one deserves
review. The same check applies to the datastores, brokers, and operators
on which critical paths depend. The rule is simple: nothing on a critical
path should depend on anything more sheddable than itself.

### Add dynamic admission control

Static quotas are always in force, which makes them blunt for conditions
that come and go. For shedding that responds to circumstances, an
admission policy can decline lower-priority workloads while the cluster is
under stress and admit them again once it recovers. A
`ValidatingAdmissionPolicy` evaluates in-process with CEL and avoids
maintaining a webhook; a policy engine such as Kyverno, available through
OperatorHub, can do the same.

The policy can be keyed to a pressure signal that an operator raises when
service-level objectives degrade, drawing on the alerts the OpenShift
monitoring stack already produces. While the cluster is stressed, it
declines pods whose assigned priority falls below a chosen threshold, and
admits them again when conditions recover.

### Prepare degraded modes and protect the control plane

The middle of an incident is the wrong time to decide which features to
disable; those decisions belong in a runbook prepared in advance.
`PodDisruptionBudget` objects help preserve a minimum number of replicas
when nodes are drained. It is also worth reviewing API Priority and
Fairness, configured through `FlowSchema` and `PriorityLevelConfiguration`,
so that a surge of agents and operators cannot overwhelm the API server
and lock operators out of the control plane during a remediation.

## Applying this to your own clusters

GitHub runs a capable site-reliability organization and still went down.
Many enterprise clusters were sized against human-speed input, are now
driven harder by their own adoption of AI tooling, and leave most of these
controls at their defaults.

A short audit is a reasonable starting point:

- Is there an inventory of workloads and an agreed criticality
  classification, and does it actually drive the priority classes, quotas,
  and policies on the cluster?
- Does the CI namespace have a `ResourceQuota`, or could a runaway fleet
  of agents consume the cluster?
- Do critical services sit behind circuit breakers in the mesh, or do they
  queue until they time out?
- Have workloads been assigned priority classes that reflect business
  importance, and has anyone audited them for priority inversions?
- Has the shedding path been exercised under load, or only assumed to
  work?
- If GitHub became unavailable mid-incident, could you still ship a fix?

For regulated industries, the last point has a compliance dimension.
Operational-resilience regimes such as the EU's DORA expect firms to plan
for continuity and to account for concentration risk in third parties they
do not control. For many institutions, GitHub is not merely a vendor but a
critical dependency.

## Conclusion

Two aspects of GitHub's response are worth emulating: the company
communicated candidly and published real figures rather than platitudes.
The broader lesson is that the human ceiling that once paced our systems is
fading, and there is little reason to expect it to return. What replaces it
has to be built deliberately. It begins with understanding the workloads
on a shared cluster and classifying them by business criticality, and it
flows from there into the OpenShift controls above: shedding at the edge,
reserving capacity by priority, auditing for inversions, and preparing
degraded modes. The teams
that do well in the coming years will not be those with the largest node
pools. They will be those that decided in advance how their clusters should
behave when they can no longer do everything, and then tested it.

## Sources

- GitHub Engineering, [The August 17 outage and the work ahead][blog].
  Source for the commit growth (1.4 billion to 2.9 billion since April),
  the infrastructure additions (more than 3 million CPU cores, 120
  petabytes of storage), the Azure platform load (12 percent in May to
  roughly 58 percent), the Actions run volume (about 115.4 million), the
  characterization of both incidents as capacity failures rather than code
  or configuration changes, the client-side retry loop, and the
  remediation items (consistent retry limits, retry budgets, and variable
  timeouts).
- GitHub status, [incident zkxwbgr0cnmx][status]. Source for the timeline
  (13:28 to 21:15 UTC on August 17, 2026, 7 hours 47 minutes), the peak
  error rates (about 20 percent for web and API, about 50 percent for
  archive and raw-content downloads), the Istio sidecar concurrency limit
  and the autoscaling policy that watched the host service rather than the
  sidecar, the four HAProxy nodes that exhausted their flow limits, the VS
  Code retry bug and its roughly tenfold amplification, and the Copilot
  Token Service increase from 7 to 9 thousand requests per second to
  between 70 and 100 thousand.

[blog]: https://github.blog/news-insights/company-news/the-august-17-outage-and-the-work-ahead/
[status]: https://www.githubstatus.com/incidents/zkxwbgr0cnmx
