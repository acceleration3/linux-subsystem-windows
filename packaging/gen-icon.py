#!/usr/bin/env python3
"""Writes packaging/lsw-manager.svg: Tux coming apart into pixels that become
the Windows logo, on a green tile. The pixels are placed here, seeded, rather
than by hand: python3 packaging/gen-icon.py packaging/lsw-manager.svg"""
import random
import sys
random.seed(7)
# Tux geometry
BODY=(92,146,46,60)    # cx, cy, rx, ry
HEAD=(92,86,34)
BELLY=(92,154,30,44)
def in_ell(x,y,e): cx,cy,rx,ry=e; return ((x-cx)/rx)**2+((y-cy)/ry)**2<=1
def in_head(x,y): cx,cy,r=HEAD; return (x-cx)**2+(y-cy)**2<=r*r
def tux_color(x,y):
    if in_ell(x,y,BELLY): return (240,243,248)
    if in_ell(x,y,BODY) or in_head(x,y): return (27,28,34)
    return None
BLUE=(46,156,240)
def mix(a,b,t): return tuple(round(a[i]+(b[i]-a[i])*t) for i in range(3))
def hexc(c): return '#%02x%02x%02x'%c
CELL=8
X0, X1 = 104, 152          # where Tux comes apart, and where the logo begins
# Logo
LX, LY, P, G = 156, 104, 38, 5
# Tux's edge, row by row: a jagged line of whole cells
rows=range(40, 216, CELL)
edges={y: X0 + CELL*random.randint(1,3) for y in rows}
# build staircase path explicitly
d='M0 0 H%d '%edges[rows[0]]
for y in rows:
    d+='H%d V%d '%(edges[y], y+CELL)
d+='H0 Z'
parts=[]
for y in rows:
    x = edges[y]
    while x < X1:
        cx, cy = x+CELL/2, y+CELL/2
        base = tux_color(cx, cy)
        t = (x - X0)/(X1 - X0)
        outside = base is None
        if outside: base = (27,28,34)
        # Past his outline the stream is thinner, but unbroken.
        keep = random.random() < (0.9 - 0.55*t) * (0.55 if outside else 1.0)
        if outside and not (52 <= y <= 196):
            keep = False
        if keep:
            size = CELL - 1 - round(2*t)
            col = mix(base, BLUE, min(1, t*1.4))
            # Funnelled: a pixel's row eases from Tux's toward the logo's.
            span = (rows[-1] + CELL) - rows[0]
            target = LY + (y - rows[0]) / span * (2*P + G)
            ease = t ** 1.6
            py = y + (target - y) * ease + random.uniform(-1, 1) * 2 * t
            px = x + random.uniform(-1, 1) * 3 * t
            nx, ny = px + (CELL - size) / 2, py + (CELL - size) / 2
            # A pixel never lands on another: converging, they thin out.
            if all(abs(nx - ox) > (size + os) / 2 + 2 or abs(ny - oy) > (size + os) / 2 + 2
                   for (ox, oy, os, _) in parts):
                parts.append((nx, ny, size, hexc(col)))
        x += CELL
svg=f'''<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <title>LSW</title>
  <!-- Tux coming apart into pixels that become the Windows logo. The pixels
       are generated (packaging/gen-icon.py), not drawn by hand. -->
  <defs>
    <linearGradient id="tile" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#4fe39a"/><stop offset="0.55" stop-color="#1fb86a"/><stop offset="1" stop-color="#0a7a45"/>
    </linearGradient>
    <radialGradient id="shine" cx="0.28" cy="0.16" r="0.8">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.38"/><stop offset="1" stop-color="#ffffff" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="win" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#5ccbff"/><stop offset="1" stop-color="#0a6fd0"/>
    </linearGradient>
    <linearGradient id="belly" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#e2e6ee"/>
    </linearGradient>
    <clipPath id="edge"><path d="{d}"/></clipPath>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="4" stdDeviation="5" flood-color="#04361f" flood-opacity="0.5"/>
    </filter>
  </defs>
  <rect x="12" y="12" width="232" height="232" rx="54" fill="url(#tile)"/>
  <rect x="12" y="12" width="232" height="232" rx="54" fill="url(#shine)"/>
  <rect x="13" y="13" width="230" height="230" rx="53" fill="none" stroke="#ffffff" stroke-opacity="0.2" stroke-width="2"/>
  <g filter="url(#shadow)">
    <g fill="url(#win)">
      <rect x="{LX}" y="{LY}" width="{P}" height="{P}" rx="3"/>
      <rect x="{LX+P+G}" y="{LY}" width="{P}" height="{P}" rx="3"/>
      <rect x="{LX}" y="{LY+P+G}" width="{P}" height="{P}" rx="3"/>
      <rect x="{LX+P+G}" y="{LY+P+G}" width="{P}" height="{P}" rx="3"/>
    </g>
    <g clip-path="url(#edge)">
      <path d="M46 128c-14 18-18 44-10 60 6 10 16 6 18-4 3-16 2-34 6-50z" fill="#16171c"/>
      <ellipse cx="{BODY[0]}" cy="{BODY[1]}" rx="{BODY[2]}" ry="{BODY[3]}" fill="#1b1c22"/>
      <circle cx="{HEAD[0]}" cy="{HEAD[1]}" r="{HEAD[2]}" fill="#1b1c22"/>
      <ellipse cx="{BELLY[0]}" cy="{BELLY[1]}" rx="{BELLY[2]}" ry="{BELLY[3]}" fill="url(#belly)"/>
      <path d="M50 200c4-10 20-14 32-8 6 3 6 10-1 12-10 3-22 4-31-4z" fill="#f6b32b"/>
      <path d="M134 200c-4-10-20-14-32-8-6 3-6 10 1 12 10 3 22 4 31-4z" fill="#f6b32b"/>
    </g>
    <ellipse cx="79" cy="80" rx="9" ry="11.5" fill="#ffffff"/>
    <ellipse cx="105" cy="80" rx="9" ry="11.5" fill="#ffffff"/>
    <ellipse cx="81" cy="82.5" rx="4.3" ry="5.6" fill="#16171c"/>
    <ellipse cx="103" cy="82.5" rx="4.3" ry="5.6" fill="#16171c"/>
    <circle cx="82.5" cy="80.5" r="1.5" fill="#ffffff"/>
    <circle cx="104.5" cy="80.5" r="1.5" fill="#ffffff"/>
    <path d="M76 98c6-6 26-6 32 0-4 10-12 15-16 15s-12-5-16-15z" fill="#f6b32b"/>
    <path d="M78 99c8 4 20 4 28 0" fill="none" stroke="#c9861a" stroke-width="2" stroke-linecap="round"/>
'''
for (x,y,s,c) in parts:
    svg+=f'    <rect x="{x:.1f}" y="{y:.1f}" width="{s}" height="{s}" rx="1" fill="{c}"/>\n'
svg+='''  </g>
</svg>
'''
open(sys.argv[1],'w').write(svg)
