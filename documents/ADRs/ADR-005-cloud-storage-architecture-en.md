# ADR-005: Cloud Storage Architecture for Measurement Data

Timegrapher measurement values must be stored so that measurement history is retrievable per watch. The first question is whether to store the data only locally on the Raspberry Pi or in the cloud; building on that, the storage technology and execution environment must be chosen. The main constraints and characteristics are as follows.


The data is small in volume and simple in structure (watch identifier + per-position measurement values).
Real-time streaming and complex relational joins are not required.
Measurement history must remain accessible even if a specific device fails or is replaced.


This feature requires an API backend through which the Raspberry Pi sends and retrieves measurement values. For storage location, local-only storage and cloud storage were considered; for the storage engine, RDS (relational) and DynamoDB (NoSQL) were considered; and for the execution environment, an always-on server (EC2) and serverless (API Gateway + Lambda) were considered.

***Decision***

**Store measurement data in the cloud, using DynamoDB as the storage layer on top of a serverless architecture, and do not apply API authentication at the current stage.**

- **Storage location:** Cloud (not local-only). Measurement values are sent from the Raspberry Pi to the cloud for storage and retrieval.
- **Storage:** AWS DynamoDB. The table uses a `watch_id` (partition key) + `measured_at` (sort key) structure.
- **API backend:** API Gateway (HTTP API) serves as the request entry point, and Lambda (Python) handles validation, storage, and retrieval logic while accessing DynamoDB.
- **API authentication:** At the current stage, the API operates without authentication (Open). However, this is an intentional decision limited to the demo/project stage, and applying API key or IAM authentication is a precondition when transitioning to actual production.

#### Note: Security Plan for Production Transition

In actual production, API key or IAM authentication will be applied. This will not affect system behavior, for the following reasons.

- **Layer separation:** Authentication is handled at the API Gateway layer by validating a request header (`x-api-key`) only. The Lambda storage/retrieval logic and the DynamoDB data structure are not changed at all.
- **Negligible performance impact:** Key validation is processed at the gateway within a few milliseconds, with no user-perceptible latency.
 
***Rationale***

**Why the cloud (instead of local-only storage)**
- **Access from multiple devices and locations:** History is not tied to a single Raspberry Pi and can be retrieved from anywhere.
- **Data durability:** Measurement history is not lost even if the Raspberry Pi fails, its SD card is corrupted, or the device is lost or replaced.
- **Centralized history:** Each watch's measurement history is gathered in one place, making the "list → detail" retrieval flow straightforward.
- **Reduced device burden:** The Raspberry Pi only measures and transmits, while the storage and retrieval load is handled by the cloud.
- **Scalability:** Even as the number of measuring devices grows, everything is managed through a single central store.

**Why DynamoDB (instead of RDS)**
- No server management is required.
- The core pattern of "retrieving a watch's history in chronological order" maps exactly to the partition key + sort key structure.
- The data is simple and has no relational joins, so the simplicity of NoSQL is a good fit.

**Why serverless (instead of an always-on server)**
- Both Lambda and API Gateway offer generous free tiers, and they run only when a request arrives, so cost is nearly zero under the intermittent measurement pattern.
- EC2 incurs charges 24 hours a day even with no traffic, which is wasteful.
- AWS manages server provisioning, patching, and scaling.

**Why API authentication is not applied at the current stage**
- This program is not a system deployed directly into production but was built for demo/project purposes. Rapid functional validation takes priority, and the endpoint's exposure scope is limited.

***Status***

Accepted


***Consequences***

**Positive**
- Can be operated at minimal cost.
- No server/infrastructure maintenance burden.
- Measurement history is preserved independently of any single device and is accessible from anywhere.
- `watch_id`-based history retrieval is fast, and it scales automatically as traffic grows.

**Negative / Trade-offs**
- **Network dependency:** Cloud storage requires an internet connection. Local-only storage would work offline, but at the cost of the benefits above (cross-device access, durability, centralized history). 
- Querying by an attribute that is not a key (partition/sort) or index requires a full Scan, which is slow (e.g. aggregating the full watch list or trend analysis over a watch's accumulated history).
- The logic is distributed across multiple components such as API Gateway, Lambda, and IAM, making initial setup more complex than a single server.
