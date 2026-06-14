# Security Policy

Piha Tank Watch takes the security of our customers' data seriously. Customers
rely on this service for their household water supply, and the system holds
personal data (email addresses) and device-ownership records. If you believe
you've found a security vulnerability, we'd like to hear about it.

## Reporting a vulnerability

Please report security issues privately by email to **security@pihatankwatch.nz**.

**Please do _not_** open a public GitHub issue, pull request, or social-media
post for a suspected vulnerability — responsible disclosure gives us a chance to
protect customers before details are public.

When you report, it helps us if you can include:

- A description of the issue and the potential impact.
- Steps to reproduce (a proof-of-concept, affected URL/endpoint, or request).
- Any relevant logs, screenshots, or sample payloads.
- How you'd like to be credited, if you'd like acknowledgement.

## What to expect

- We'll **acknowledge your report as soon as we can** and let you know it's
  being looked at.
- We'll give you our **initial assessment** (whether we can reproduce it and how
  serious we think it is) as quickly as we're able.
- We'll keep you updated on remediation progress and let you know when a fix
  ships.

We're a small team, so we don't promise hard turnaround times — but we will
always reply to a good-faith report.

## Scope

In scope:

- The web app (`app.pihatankwatch.nz`) and admin portal
  (`admin.pihatankwatch.nz`).
- The public API (`api.pihatankwatch.nz`).
- The marketing site (`pihatankwatch.nz`).
- The device firmware and its provisioning/claim flow.

Out of scope (please don't test these):

- Denial-of-service / volumetric or load testing against production.
- Social engineering of staff, customers, or trade partners.
- Physical attacks against hardware you don't own.
- Automated scanner output without a demonstrated, reproducible impact.

## Safe harbour

If you make a good-faith effort to follow this policy, we will not pursue or
support legal action against you for your research, and we'll treat your report
as authorised testing.

## Our commitments

- We'll respond to every good-faith report.
- We won't take legal action against good-faith researchers who follow this
  policy.
- We'll credit reporters who'd like acknowledgement (with your permission).

---

_Contact: security@pihatankwatch.nz_
