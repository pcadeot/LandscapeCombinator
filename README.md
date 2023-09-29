# Landscape Combinator

![Landscape Picture with Road](Gallery/picture.png?raw=true "Landscape Picture with Road")
![Landscape Picture with Forests](Gallery/picture2.png?raw=true "Landscape Picture with Forests")
![Plugin Use Example](Gallery/capture.png?raw=true "Plugin Use Example")

## Description

Landscape Combinator is an Unreal Engine 5.3 plugin that enables you to create landscapes from
real-world data in a few simple steps:

* Create landscapes from real-world heightmaps (with a basic landscape material to visualize them),
* Create landscape splines from [OSM](https://www.openstreetmap.org) data for roads, rivers, etc.
* Automatically generate buildings using splines representing buildings outlines in [OSM](https://www.openstreetmap.org),
* Add procedural foliage (such as forests) based on [OSM](https://www.openstreetmap.org) data.

When creating a landscape for a real-world location, the plugin will adjust its position and scale
correctly in a planar world. You can thus create several real-world landscapes of various quality and
have them placed and scaled correctly with respect to one another. In the places where the landscapes
overlap, you can use a button to automatically flatten/erase data from the low quality landscape
(with the `Dig` button explained below).


## Installation

You can use the plugin sources directly from here for personal projects.
Copy the content of this repository to a folder called `LanscapeCombinator` inside
the Plugins folder of your project. Then launch your project and click `Yes` when
Unreal Engine asks whether you want to compile the module.

If you plan to use [Viewfinder Panoramas](http://viewfinderpanoramas.org/) heightmaps, please install
[7-Zip](https://www.7-zip.org/download.html) and add it to your Windows PATH environment variable as follows.
Press the Windows button on your keyboard or on the bottom left of your screen, and write PATH with your keyboard.
Click on "Edit the system environment variables", then press the "Environment Variables" button. Click on "PATH" in
the "System Variables" list, and press the "Edit" button at the bottom. Click on "New", and paste the path to your
7-Zip installation (which is typically C:\Program Files\7-Zip).

For commercial projects, please check the [Unreal Engine Marketplace](https://www.unrealengine.com/marketplace/en-US/product/landscape-combinator).


## Creating Landscapes

* After loading the plugin, open the plugin window using the plugin button or in the `Tools` menu.
* Some heightmaps lines examples are already there for you to try, or you can add a new heightmap line
  using the heightmap provider of your choosing. Heightmap lines are saved automatically in the HeightMapListV1 file in the Saved directory of your project.
* Press the `Create Landscape` button for the landscape(s) of your chosing.
  It might look like as if nothing is happening, because it can take a while to convert the heightmaps.
  Please monitor the progress in the Output Log (in the LogLandscapeCombinator category) and make sure there are
  no errors.
  After a while, you should see your landscape appear in your editor.
* Go to the `Landscape Mode` of Unreal Engine in order to paint the landscape. You can use the basic landscape material
  `MI_Landscape` provided by the plugin, which as an auto layer called `Mix` (with adjustable snow, grass, cliff, and road materials).
* If you have created several landscapes that overlap, for instance a large one with few details, and a small one with many details,
  you can press the `Dig` button on the small landscape to make a hole in the large landscape in the rectangle where they overlap.


### Technical Notes

The plugin places landscapes in a planar world using EPSG 4326 coordinates.
If your original data does not use EPSG 4326, you can choose to reproject it to EPSG 4326 (with a checkbox).
The reprojection is done thanks to [GDAL](https://gdal.org/) and can introduce some artefacts such as staircases in your landscape.

If you choose not to reproject, the landscapes will be nicer, but you will loose coordinate precision within your landscape
when you add landscape splines or procedural foliage from OSM data (see next sections) which use EPSG 4326 coordinates.

Finally, if you want to find the location of a real-world place in your landscape inside the editor,
find the [EPSG 4326 coordinates (Long, Lat)](https://epsg.io/map#srs=4326) (I found the site works better on Chrome than on Firefox)
  and convert them to Unreal Engine coordinates as follows:
```C
x = (Long - WorldOriginX) * WorldWith (in km) * 1000 * 100 / 360
y = (WorldOriginY - Lat) * WorldHeight (in km) * 1000 * 100 / 180 // note the signs are opposite from the line above
```


## Adding Roads, Rivers, Buildings (using Splines or Landscape Splines)

* You can add (regular) splines or landscape splines from OSM data on the landscapes you created.
* In the Content Drawer, make sure that "Show C++ Classes" is checked in the settings.
* Search for `SplinesBuilder`, which is a C++ class in the Landscape Combinator plugin, and drag it into your level.
* In the details panel of your newly created actor, select the Splines Source as you wish.
  You can either use a premade query, such as Roads, Buildings, or you can write the query yourself using Overpass syntax.
  For instance, the premade query for Roads is the same as using "Overpass Short Query" with the query:
  `way["highway"]["highway"!~"path"]["highway"!~"track"];`.
* Select whether you want landscape splines, or regular spline components.
* Enter the label of the landscape used as a support to generate the splines.
* Press the "Generate Splines" button.

If you want to create roads, you can generate landscape splines, and then go to
`Landscape Mode > Manage > Splines` to control the spline parameters as usual.
You can choose to paint using a layer (fill the `Layer Name` field in the details panel after selecting all control points and then all segments).
If you use the `MI_Landscape` material for your landscape, you can try `Road` as a `Layer Name` to paint roads.

For rivers, you can select all the segments, and then add a mesh on the landscape splines in the details panel to represent water.

For buildings, it is recommended that you use regular splines (not landscape splines).
When you generate regular splines, they are added to as Spline Components in your `SplinesBuilder` actor.
Use the "Toggle Linear" button if you
want buildings with straight walls. Then, drag a `BuildingsFromSplines`
actor in your level. Select the parameters you want for your buildings (windows and doors meshes, materials, number of floors, etc.).
You may also enter tags to filter the spline components from which buildings are generated.
Keep the tags set to `None` if you want to generate buildings on all the spline components
existing in your level. Once you are satisfied with your settings, press the "Generate Buildings" button.


## Procedural Foliage

There are (at least) two kinds of foliage that you may add. Foliage that you want almost everywhere, such as grass or rocks,
can be added using the Landscape Grass Types defined in the landscape material `MI_Landscape`.

Other foliage such as trees can be spawned only in some areas (using real-world data or any kind of vector file supported
by [GDAL](https://gdal.org/). This is done thanks to a new PCG node called "OGRFilter" that filters out PCG points based
on real-world geometries of forests or other areas.


## Future Work

* The plugin tab to spawn landscapes will be refactored into actors and actor components for more modularity.

Feel free to open an issue if there is a feature you would like see.

## Heightmap Providers

For now we only support downloading heightmaps from the following sources. Please check their licenses
before using their heightmaps in your projects.

* [RGE ALTI® 1m France](https://geoservices.ign.fr/rgealti)

* [Elevation API IGN1m](https://elevationapi.com/) (this should be the same data as RGE ALTI® 1m)

* [swissALTI3D](https://www.swisstopo.admin.ch/en/geodata/height/alti3d.html)
	To use swissALTI3D, please go to their web page, and on the bottom, choose "Selection by Rectangle".
	Click on "New Rectangle", and select the rectangle area that you want. Choose the resolution
	that you want (0.5m is the highest precision, or 2m), and click on "Search".
	Then, click on "Export all links" and download the CSV file. In the Landscape Combinator
	plugin, you then provide the path to the CSV file on your computer.

* [Viewfinder Panoramas](http://viewfinderpanoramas.org/)


## Data for Landscape Splines and Procedural Foliage

If you use [OSM](https://www.openstreetmap.org) data or [Overpass API](https://overpass-api.de/) to get data for
splines or procedural foliage, please check their licenses.
