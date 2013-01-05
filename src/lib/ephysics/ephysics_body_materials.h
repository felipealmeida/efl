#ifndef _EPHYSICS_BODY_MATERIALS_H
#define _EPHYSICS_BODY_MATERIALS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _EPhysics_Body_Material_Props EPhysics_Body_Material_Props;
struct _EPhysics_Body_Material_Props {
     EPhysics_Body_Material material;
     double density;
     double friction;
     double restitution;
};

const EPhysics_Body_Material_Props ephysics_material_props [] = {
       {EPHYSICS_BODY_MATERIAL_CUSTOM, 1.0, 0.5, 0.0},
       {EPHYSICS_BODY_MATERIAL_CONCRETE, EPHYSICS_BODY_DENSITY_CONCRETE,
          EPHYSICS_BODY_FRICTION_CONCRETE, EPHYSICS_BODY_RESTITUTION_CONCRETE},
       {EPHYSICS_BODY_MATERIAL_IRON, EPHYSICS_BODY_DENSITY_IRON,
          EPHYSICS_BODY_FRICTION_IRON, EPHYSICS_BODY_RESTITUTION_IRON},
       {EPHYSICS_BODY_MATERIAL_PLASTIC, EPHYSICS_BODY_DENSITY_PLASTIC,
          EPHYSICS_BODY_FRICTION_PLASTIC, EPHYSICS_BODY_RESTITUTION_PLASTIC},
       {EPHYSICS_BODY_MATERIAL_POLYSTYRENE, EPHYSICS_BODY_DENSITY_POLYSTYRENE,
          EPHYSICS_BODY_FRICTION_POLYSTYRENE,
          EPHYSICS_BODY_RESTITUTION_POLYSTYRENE},
       {EPHYSICS_BODY_MATERIAL_RUBBER, EPHYSICS_BODY_DENSITY_RUBBER,
          EPHYSICS_BODY_FRICTION_RUBBER, EPHYSICS_BODY_RESTITUTION_RUBBER},
       {EPHYSICS_BODY_MATERIAL_WOOD, EPHYSICS_BODY_DENSITY_WOOD,
          EPHYSICS_BODY_FRICTION_WOOD, EPHYSICS_BODY_RESTITUTION_WOOD}
};

#ifdef __cplusplus
}
#endif

#endif