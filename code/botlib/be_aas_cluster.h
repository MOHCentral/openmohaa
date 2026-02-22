
#ifndef BE_AAS_CLUSTER_H
#define BE_AAS_CLUSTER_H

// Prototypes from be_aas_cluster.c

void AAS_InitClustering(void); // Added missing prototype
void AAS_RemoveClusterAreas(void);
void AAS_ClearCluster(int clusternum);
void AAS_RemovePortalsClusterReference(int clusternum);
int AAS_UpdatePortal(int areanum, int clusternum);
int AAS_FloodClusterAreas_r(int areanum, int clusternum);
int AAS_FloodClusterAreasUsingReachabilities(int clusternum);
void AAS_NumberClusterPortals(int clusternum);
void AAS_NumberClusterAreas(int clusternum);
int AAS_FindClusters(void);
void AAS_CreatePortals(void);
int AAS_MapContainsTeleporters(void);
int AAS_NonConvexFaces(aas_face_t *face1, aas_face_t *face2, int side1, int side2);
qboolean AAS_CanMergeAreas(int *areanums, int numareas);
qboolean AAS_NonConvexEdges(aas_edge_t *edge1, aas_edge_t *edge2, int side1, int side2, int planenum);
qboolean AAS_CanMergeFaces(int *facenums, int numfaces, int planenum);
void AAS_ConnectedAreas_r(int *areanums, int numareas, int *connectedareas, int curarea);
qboolean AAS_ConnectedAreas(int *areanums, int numareas);
int AAS_GetAdjacentAreasWithLessPresenceTypes_r(int *areanums, int numareas, int curareanum);
int AAS_CheckAreaForPossiblePortals(int areanum);

#endif
