#pragma once

#include <string>
#include "exactObjectList.hpp"
#include "math.hpp"

struct Worm;
struct Game;
struct WormWeapon;
struct NObject;

/*
nObjects are non-weapon objects.
Those are things like worm gibs, dropped shells, blood, but also additional
weapon particles like Chiquita bananas dropped by Chiquita Bomb. Like other
types of objects, they are indexed by their order in the array (counting started
from 0).
*/
struct NObjectType {
  NObject& create(
      Game& game,
      fixedvec vel,
      fixedvec pos,
      int color,
      int ownerIdx,
      WormWeapon* firedBy);
  NObject& create1(
      Game& game,
      fixedvec vel,
      fixedvec pos,
      int color,
      int ownerIdx,
      WormWeapon* firedBy);
  void create2(
      Game& game,
      int angle,
      fixedvec vel,
      fixedvec pos,
      int color,
      int ownerIdx,
      WormWeapon* firedBy);

  /*
  Additional worm detect distance for the bullet. This setting affects the
  distance at which an object hits a worm (but - unlike in wObject - it does not
  determine the starting distance from the player). Add more for "bigger"
  bullets or things like proximity detonators. Note: if detectDistance < 0, then
  the worm will not receive damage from the object and also blowAway parameter
  will not work.
  */
  int detectDistance;
  /*
  Gravity of the particle.
  */
  fixed gravity;
  /*
  Initial speed of the particle.
  Note: this parameter works only for nObjects created as:
  a) splinters with "splinterScatter" parameter set to 0;
  b) trails with "partTrailType" parameter set to 0.
  Note: for splinters with "splinterScatter" parameter set to 1 and trails with
  "partTrailType" parameter set to 1, the speed of nObject is affected only by
  wObject actual speed!
  */
  int speed;
  /*
  (Negative) variation in initial speed of the particle.
  */
  int speedV;
  /*
  Spread of the particle. This works by adding a random direction vector of
  random length to current speed vector of the projectile. Note: if you set its
  value to > 1 or < -1, then more than 1 direction vector will be applied, so
  that the projectiles will have a variable initial velocity after firing.
  */
  int distribution;
  /*
  Force affecting the hit worm. This will also work if the object has
  "wormDestroy" set to false; it will simply not disappear and push the worm
  continuously. Note: works only if the particle is moving and detectDistance ⩾
  0 and hitDamage ≠ 0!
  */
  int blowAway;
  /*
  Speed multiply on hitting rock/dirt obstacle or the edge of the map. After
  every bounce, the projectile will get this percentage of its original speed.
  Note: lack of "bounceFriction" property here!
  Note: unlike in wObject, setting negative value for bounce will not make
  nObject pass through walls.
  */
  int bounce;
  /*
  Damage inflicted on worm which was hit.
  Note: If the object has "wormDestroy" property set to "false", this will be
  applied each frame the collision still occurs, leading to potentially huge
  damage values. Note: if you set negative value for it, you will have healing
  effect.
  */
  int hitDamage;
  /*
  Whether the object should explode (produce a sObject) on worm collision. Works
  independently of "wormDestroy" parameter, which means that the object will
  explode also even if "wormDestroy" is set to "false". Note: this property
  doesn't work if nObject "hitDamage" parameter equals 0!
  */
  bool wormExplode;
  /*
  Whether the object should explode on ground collision.
  Note: works only when the object is no longer in motion.
  */
  bool explGround;
  /*
  Whether the object should collide with the worm and get removed.
  Note: this property doesn't work if nObject "hitDamage" parameter equals 0!
  */
  bool wormDestroy;
  /*
  How many blood particles (nObject6) should be created on worm hit, divided
  by 2. So, if you set it to "10", then 5 blood particles will be created on
  worm hit (per each frame - which means that the amount of blood particles is
  affected by "wormDestroy" parameter). Note: this property doesn't work if
  nObject "hitDamage" parameter equals 0!
  */
  int bloodOnHit;
  /*
  First sprite of animation used for nObject.
  If -1 or 0, it will be a single pixel using a colour indicated in:
  a) "splinterColour" property (used in in the other wObject or nObject which
  produces given nObject) - if this "splinterColour" property is set to 2 or
  more; b) "colorBullets" property - if "splinterColour" parameter mentioned
  above is set to 0 or 1.
  */
  int startFrame;
  /*
  Amount of sprites to use to animate the object, starting with "startFrame".
  Note: Animation begins on random frame, so is suitable really only for objects
  which have animation cycle which looks good regardless of what frame it
  starts. Think things like spinning grenades, mines, pulsing items, etc. Note:
  lack of "loopAnim" parameter for nObjects! Note: if numFrames > 0, then the
  animation cycle works only if the particle is moving; when the particle stops
  moving, the animation cycle also stops.
  */
  int numFrames;
  /*
  When set to true, this makes the object to be drawn onto the map and object
  itself gets removed from the game. This is how mountains of shells and body
  parts are created in liero and also the reason why they clutter up the
  bunnyhoops. Note: works only on collsision with rock / dirt / edge of the map
  if explGround is set "true" and the object is no longer in motion!
  */
  bool drawOnMap;
  /*
  Color of the object. Works only if u use a pixel (startframe: -1 or 0) for a
  nObject and only if "splinterColour" parameter (used in in the other wObject
  or nObject which produced your nObject) is set to 0 or 1 (otherwise, your
  nObject will have color indicated in "splinterColour" parameter used in this
  other object).
  */
  int colorBullets;
  /*
  Which special object (sObject) to use on explosion. Set -1 for none.
  */
  int createOnExp;
  /*
  Whether the object is affected by explosions' push force (on collision with
  sObject). Note: works only if the colliding sObject has got detectRange > 0
  and damage ≠ 0 and blowAway ≠ 0!
  */
  bool affectByExplosions;
  /*
  Which dirt mask to use on object explosion. Set -1 is none.
  */
  int dirtEffect;
  /*
  Amount of nObjects to create on explosion. The wObject must actually explode,
  for example if "wormExplode" is set to false and "wormCollide" is set to true,
  no nObjects will be created.
  */
  int splinterAmount;
  /*
  Color used on nObjects (produced as splinters) when they are a single pixel
  (startFrame -1 or 0). If splinterColour is set to 0 or 1, then splinters will
  have a colour indicated in nObject "colorBullets" property. Note: if
  splinterColour is set 2 or more, nObject splinter will actually use two
  colours: the one indicated in this parameter, and also the previous one. So,
  in this case, splinters will use colour 13 and 12.
  */
  int splinterColour;
  /*
  Type of nObjects used when an explosion occurs. This refers to index of the
  nObject in the array (counting started from 0), so if you change the order of
  nObjects, something else will be used.
  */
  int splinterType;
  /*
  A nObject-specific property; when set to true, the nObject will trail blood
  particles. Note: when nObject stops moving in the air, it keeps creating blood
  particles on its trail. However, if nObject is spawned or "trapped" inside
  rock or dirt on the map, then it stops creating blood particles on its trail.
  */
  bool bloodTrail;
  /*
  Delay between blood particles.
  */
  int bloodTrailDelay;
  /*
  sObject (special Object) used as a trail. Set -1 for none.
  Note: when nObject stops moving, it keeps creating sObjects on its trail.
  However, if nObject is spawned or "trapped" inside rock or dirt on the map,
  then it stops creating sObjects on its trail.
  */
  int leaveObj;
  /*
  Delay time (in frames) between creating trailing sObjects.
  Note: the delay is not referred to object's lifetime but game ticks, which
  means that it is measured in relation to the time elapsed since the game
  started, not since the object was created. Note: If "repeat" is set to greater
  than 1, sObjects will be created in "batches", creating intermittent lines
  instead of denser lines. This is probably a bug or unintended behavior. Note:
  notice that nObjects cannot trail other nObjects.
  */
  int leaveObjDelay;
  /*
  Time to explode in frames. When set to 0 there will be no explosion at all.
  Any positive value will cause creation of a designated sObject indicated in
  createOnExp parameter (if not -1). Note: it is not recommended to set negative
  value for this property. Note: it is not recommended to set timeToExplo >
  32767.
  */
  int timeToExplo;
  /*
  Maximum (negative) variation of time to explode in frames.
  */
  int timeToExploV;

  int id;
  std::string idStr;
};

struct NObject : ExactObjectListBase {
  void process(Game& game);

  fixedvec pos, vel;
  int timeLeft;
  // int id;
  NObjectType const* type;
  int ownerIdx;
  int curFrame;

  // STATS
  WormWeapon* firedBy;
  bool hasHit;
};
