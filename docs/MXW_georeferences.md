# MXW\_georeferences

## Overview

This extension adds a way to store georeferencing information in a Potree point cloud file.
While the `metadata.json` file already has a field called `projection`,
this field is supposed to be filled-in by hand through the command line and
accepts any value.
While not documented, the expected format of this value is a string accepted by [proj4](http://proj4js.org/),
i.e. *proj or wkt strings, or PROJJSON objects*.

We instead introduce a JSON object that can describe the projection in multiple formats, including proj.4 and WKT.

## Adding georeference info to a point cloud

Georeferencing is defined in a Potree metadata file by adding an `MXW_georeferences` property defining an object called `georeference`.
This object then contains the georeferencing information in various formats.
This extension defines two formats: `wkt` and `proj4`.

Files that have georeference information in multiple formats **must** store the
same information in each format.
Implementations **may** use any of the formats present.
The information in `georeference` **should** take precedence over `projection`, if both are present.

### `wkt`

The `wkt` object contains the OGC Coordinate System WKT in the string property `coordinateSystem` and the Math Transform WKT in the string property `mathTransform`.
`mathTransform` can be `null`.

#### LAS
*This section is non-normative.*

These two fields correspond to the WKT VLRs (variable-length record) found in a LAS file.

The VLR `User ID="LASF_Projection", Record ID=2111` contains the UTF-8 encoded value of `wkt.mathTransform`.
The VLR `User ID="LASF_Projection", Record ID=2112` contains the UTF-8 encoded value of `wkt.coordinateSystem`.

### `proj4`

The `proj4` string contains a proj.4 string.
`georeference.proj4` **should** take precedence over `projection` and both **must** have the same value.

## Schema Example

```json
{
  "version": "2.0",
  ...
  "MXW_georeferences": {
    "georeference": {
      "wkt": {
        "coordinateSystem": "COMPD_CS[\"NAD83(2011) / Washington North (ftUS) + NAVD88 height (ftUS) - Geoid18 (ftUS)\",PROJCS[\"NAD83(2011) / Washington North (ftUS)\",GEOGCS[\"NAD83(2011)\",DATUM[\"NAD83 (National Spatial Reference System 2011)\",SPHEROID[\"GRS 1980\",6378137,298.257222101,AUTHORITY[\"EPSG\",\"7019\"]],AUTHORITY[\"EPSG\",\"1116\"]],PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],AUTHORITY[\"EPSG\",\"6318\"]],PROJECTION[\"Lambert_Conformal_Conic_2SP\"],PARAMETER[\"standard_parallel_1\",48.73333333333333],PARAMETER[\"standard_parallel_2\",47.5],PARAMETER[\"latitude_of_origin\",47],PARAMETER[\"central_meridian\",-120.8333333333333],PARAMETER[\"false_easting\",1640416.667],PARAMETER[\"false_northing\",0],UNIT[\"US survey foot\",0.3048006096012192,AUTHORITY[\"EPSG\",\"9003\"]],AXIS[\"X\",EAST],AXIS[\"Y\",NORTH],AUTHORITY[\"EPSG\",\"6597\"]],VERT_CS[\"NAVD88 height (ftUS) - Geoid18 (ftUS)\",VERT_DATUM[\"North American Vertical Datum 1988\",2005,AUTHORITY[\"EPSG\",\"5103\"]],UNIT[\"US survey foot\",0.3048006096012192,AUTHORITY[\"EPSG\",\"9003\"]],AXIS[\"Up\",UP],AUTHORITY[\"EPSG\",\"6360\"]]]",
        "mathTransform": null
      },
      "proj4": "+proj=gnom +lat_0=90 +lon_0=0 +x_0=6300000 +y_0=6300000 +ellps=WGS84 +datum=WGS84 +units=m +no_defs"
    }
  }
}
```
