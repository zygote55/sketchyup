# Offline deterministic solar position

R067.a, 2026-10-06. `noaa-meeus-geometric-v1` calculates the geocentric solar
center direction from explicit latitude, east-positive longitude and a resolved
UTC instant. It uses the Julian-century equations documented by NOAA's
[Meeus-based calculator](https://gml.noaa.gov/grad/solcalc/calcdetails.html),
with exact Gregorian date conversion. Supported resolved UTC dates are 1901–2099.
The calculation is offline and does not access the OS time zone, maps or a provider.

Civil input includes date, hour, minute, second and an explicit UTC offset in whole
minutes between −14 and +14 hours. Invalid calendar dates, leap seconds and a
resolved instant outside the supported interval are rejected. The offset is used
as entered, including any daylight-saving adjustment chosen by the user; it is not
an IANA zone rule and is never inferred. The same instant and location produce the
same result regardless of the machine's locale or time zone.

Latitude is within ±90°, longitude and model north rotation within ±180°.
Direction points toward the sun in the native Z-up model. With zero rotation,
east is +X and north is +Y. Positive north rotation turns north clockwise toward
+X; reported geographic azimuth remains clockwise from north. At the vertical
singularity azimuth is explicitly undefined and its placeholder value is zero.
Polar inputs retain the convention defined by their chosen meridian.

Elevation and horizon classification use the geometric solar center without
atmospheric refraction, terrain, solar-disc radius or observer elevation. Thus a
sunrise label derived from this result means the center crossing a level geometric
horizon, not NOAA's apparent sunrise convention. Direction remains valid at night;
consumers enable direct sunlight and shadows only above the geometric horizon.
This version makes no observational accuracy or legal certification claim.

Independent numerical fixtures contain 239 cached rows from NOAA's published
[day spreadsheet](https://gml.noaa.gov/grad/solcalc/NOAA_Solar_Calculations_day.ods),
for 40° N, 105° W, 2010-06-21 at UTC−07:00. Only numeric reference values are stored;
no spreadsheet recalculation or network access is used by tests. The downloaded
ODS SHA-256 is `d013d001a7620645c3d1dd23b8d876f76d6fbae577df295330cb494a0c6cd3b5`. Agreement is
checked to 1e-7 degrees/minutes. Additional tests cover UTC equivalence, negative
Unix epochs, quarter-hour offsets, north rotation, both poles, date boundaries,
dateline equivalence, unit vectors and invalid finite/calendar ranges.

The Qt-free library is a foundation. Document/history/scene persistence, shared
commands, native controls and viewport shadow rendering follow in subsequent R067
layers; this layer does not change the existing viewport lighting.
