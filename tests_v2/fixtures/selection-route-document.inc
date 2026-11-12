// General-purpose geometry fixture; inputs are selected with actual UI events.
namespace selectionfixture {
static const std::string documentJson=std::string(R"SelectionFixture({
  "activeGroupId": "00000000-0000-4000-8000-000000050001",
  "assets": [],
  "documentId": "00000000-0000-4000-8000-000000050002",
  "entities": [
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000000002",
      "datum": false,
      "displayName": "top_XY",
      "editPolicy": "source",
      "groupId": null,
      "id": "00000000-0000-4000-8000-000000000003",
      "kind": "work_plane",
      "partProperties": null,
      "revision": 1,
      "visibility": "hidden"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000000004",
      "datum": false,
      "displayName": "front_XZ",
      "editPolicy": "source",
      "groupId": null,
      "id": "00000000-0000-4000-8000-000000000005",
      "kind": "work_plane",
      "partProperties": null,
      "revision": 4,
      "visibility": "hidden"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000000006",
      "datum": false,
      "displayName": "side_YZ",
      "editPolicy": "source",
      "groupId": null,
      "id": "00000000-0000-4000-8000-000000000007",
      "kind": "work_plane",
      "partProperties": null,
      "revision": 3,
      "visibility": "hidden"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050003",
      "datum": false,
      "displayName": "点A",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050004",
      "kind": "point",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050005",
      "datum": false,
      "displayName": "点B",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050006",
      "kind": "point",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050007",
      "datum": false,
      "displayName": "点C",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050008",
      "kind": "point",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-00000005000a",
      "datum": false,
      "displayName": "直線A",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-00000005000b",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-00000005000d",
      "datum": false,
      "displayName": "直線B",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-00000005000e",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050010",
      "datum": false,
      "displayName": "軸",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050011",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050013",
      "datum": false,
      "displayName": "経路",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050014",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050016",
      "datum": false,
      "displayName": "円",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050017",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-00000005001c",
      "datum": false,
      "displayName": "輪郭A",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-00000005001d",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-00000005001e",
      "datum": false,
      "displayName": "面A",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-00000005001f",
      "kind": "guide_surface",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050020",
      "datum": false,
      "displayName": "部品A",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050021",
      "kind": "part",
      "partProperties": {
        "manufacturing": null,
        "purpose": "finished_model"
      },
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050026",
      "datum": false,
      "displayName": "輪郭B",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050027",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050028",
      "datum": false,
      "displayName": "面B",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050029",
      "kind": "guide_surface",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-00000005002a",
      "datum": false,
      "displayName": "部品B",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-00000005002b",
      "kind": "part",
      "partProperties": {
        "manufacturing": null,
        "purpose": "finished_model"
      },
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-00000005003)SelectionFixture")+R"SelectionFixture(0",
      "datum": false,
      "displayName": "輪郭C",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050031",
      "kind": "wire",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000050032",
      "datum": false,
      "displayName": "面C",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000050033",
      "kind": "guide_surface",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000060000",
      "datum": false,
      "displayName": "基準平面1",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000060001",
      "kind": "work_plane",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    },
    {
      "construction": false,
      "createdBy": "00000000-0000-4000-8000-000000060002",
      "datum": false,
      "displayName": "基準平面2",
      "editPolicy": "source",
      "groupId": "00000000-0000-4000-8000-000000050001",
      "id": "00000000-0000-4000-8000-000000060003",
      "kind": "work_plane",
      "partProperties": null,
      "revision": 0,
      "visibility": "visible"
    }
  ],
  "features": [
    {
      "definition": {
        "inputs": [],
        "isOriginPlane": true,
        "method": 0,
        "normal": {
          "x": 0,
          "y": 0,
          "z": 1
        },
        "offset": {
          "expression": "",
          "quantity": "scalar",
          "value": 0
        },
        "origin": {
          "x": 0,
          "y": 0,
          "z": 0
        },
        "uDirection": {
          "x": 1,
          "y": 0,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "top_XY",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000000002",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000000003",
          "key": "plane",
          "kind": "work_plane"
        }
      ],
      "revision": 0,
      "type": "create_work_plane"
    },
    {
      "definition": {
        "inputs": [],
        "isOriginPlane": true,
        "method": 0,
        "normal": {
          "x": 0,
          "y": 1,
          "z": 0
        },
        "offset": {
          "expression": "",
          "quantity": "scalar",
          "value": 0
        },
        "origin": {
          "x": 0,
          "y": 0,
          "z": 0
        },
        "uDirection": {
          "x": 0,
          "y": 0,
          "z": 1
        }
      },
      "derivedGroupId": null,
      "displayName": "front_XZ",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000000004",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000000005",
          "key": "plane",
          "kind": "work_plane"
        }
      ],
      "revision": 0,
      "type": "create_work_plane"
    },
    {
      "definition": {
        "inputs": [],
        "isOriginPlane": true,
        "method": 0,
        "normal": {
          "x": 1,
          "y": 0,
          "z": 0
        },
        "offset": {
          "expression": "",
          "quantity": "scalar",
          "value": 0
        },
        "origin": {
          "x": 0,
          "y": 0,
          "z": 0
        },
        "uDirection": {
          "x": 0,
          "y": 1,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "side_YZ",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000000006",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000000007",
          "key": "plane",
          "kind": "work_plane"
        }
      ],
      "revision": 0,
      "type": "create_work_plane"
    },
    {
      "definition": {
        "position": {
          "x": -25,
          "y": -35,
          "z": 6
        },
        "sourcePlaneId": null,
        "expressions": {
          "x": {
            "expression": "",
            "quantity": "length",
            "value": -25
          },
          "y": {
            "expression": "",
            "quantity": "length",
            "value": -35
          },
          "z": {
            "expression": "",
            "quantity": "length",
            "value": 6
          }
        }
      },
      "derivedGroupId": null,
      "displayName": "点A",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050003",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050004",
          "key": "point",
          "kind": "point"
        }
      ],
      "revision": 0,
      "type": "create_point"
    },
    {
      "definition": {
        "position": {
          "x": 0,
          "y": -30,
          "z": 12
        },
        "sourcePlaneId": null,
        "expressions": {
          "x": {
            "expression": "",
            "quantity": "length",
            "value": 0
          },
          "y": {
            "expression": "",
            "quantity": "length",
            "value": -30
          },
          "z": {
            "expression": "",
            "quantity": "length",
            "value": 12
          }
        }
      },
      "derivedGroupId": null,
      "displayName": "点B",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050005",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050006",
          "key": "point",
          "kind": "point"
        }
      ],
      "revision": 0,
      "type": "create_point"
    },
    {
      "definition": {
        "position": {
          "x": -10,
          "y": -10,
          "z": 21
        },
        "sourcePlaneId": null,
        "expressions": {
          "x": {
            "expression": "",
            "quantity": "length",
            "value": -10
          },
          "y": {
            "expression": "",
            "quantity": "length",
            "value": -10
          },
          "z": {
            "expression": "",
            "quantity": "length",
            "value": 21
          }
        }
      },
      "derivedGroupId": null,
      "displayName": "点C",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050007",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050008",
          "key": "point",
          "kind": "point"
        }
      ],
      "revision": 0,
      "type": "create_point"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneI)SelectionFixture"+R"SelectionFixture(d": null,
        "wire": {
          "closed": false,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": -35,
                "y": -20,
                "z": 0
              },
              "end": {
                "x": -15,
                "y": -20,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050009"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "直線A",
      "enabled": true,
      "id": "00000000-0000-4000-8000-00000005000a",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-00000005000b",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": false,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": -15,
                "y": -20,
                "z": 0
              },
              "end": {
                "x": -15,
                "y": -5,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-00000005000c"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "直線B",
      "enabled": true,
      "id": "00000000-0000-4000-8000-00000005000d",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-00000005000e",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": false,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": 0,
                "y": -20,
                "z": -10
              },
              "end": {
                "x": 0,
                "y": -20,
                "z": 25
              },
              "id": "00000000-0000-4000-8000-00000005000f"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "軸",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050010",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050011",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": false,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": 10,
                "y": -20,
                "z": 0
              },
              "end": {
                "x": 25,
                "y": -20,
                "z": 15
              },
              "id": "00000000-0000-4000-8000-000000050012"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "経路",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050013",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050014",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": true,
          "segments": [
            {
              "type": "circle",
              "center": {
                "x": 30,
                "y": 30,
                "z": 0
              },
              "normal": {
                "x": 0,
                "y": 0,
                "z": 1
              },
              "xDirection": {
                "x": 1,
                "y": 0,
                "z": 0
              },
              "radiusMm": 5,
              "id": "00000000-0000-4000-8000-000000050015"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "円",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050016",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050017",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": true,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": -50,
                "y": -5,
                "z": 0
              },
              "end": {
                "x": -30,
                "y": -5,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050018"
            },
            {
              "type": "line",
              "start": {
                "x": -30,
                "y": -5,
                "z": 0
              },
              "end": {
                "x": -30,
                "y": 10,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050019"
            },
            {
              "type": "line",
              "start": {
                "x": -30,
                "y": 10,
                "z": 0
              },
              "end": {
                "x": -50,
                "y": 10,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-00000005001a"
            },
            {
              "type": "line",
              "start": {
                "x": -50,
                "y": 10,
                "z": 0
              },
              "end": {
                "x": -50,
                "y": -5,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-00000005001b"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "輪郭A",
      "enabled": true,
      "id": "00000000-0000-4000-8000-00000005001c",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-00000005001d",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "chains": [
          {
            "segments": [
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-000000050018",
                "wireEntityId": "00000000-0000-4000-8000-00000005001d"
              },
              {
                "range": {)SelectionFixture"+R"SelectionFixture(
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-000000050019",
                "wireEntityId": "00000000-0000-4000-8000-00000005001d"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-00000005001a",
                "wireEntityId": "00000000-0000-4000-8000-00000005001d"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-00000005001b",
                "wireEntityId": "00000000-0000-4000-8000-00000005001d"
              }
            ],
            "reversed": [
              false,
              false,
              false,
              false
            ]
          }
        ],
        "roles": [
          0
        ],
        "method": 0,
        "fourEdgeStyle": 0,
        "lockSectionOrder": false,
        "offsetDistanceMm": 0,
        "revolveAngleRad": 0,
        "revolveAxisDirection": {
          "x": 0,
          "y": 0,
          "z": 1
        },
        "revolveAxisPoint": {
          "x": 0,
          "y": 0,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "面A",
      "enabled": true,
      "id": "00000000-0000-4000-8000-00000005001e",
      "inputEntityIds": [
        "00000000-0000-4000-8000-00000005001d"
      ],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-00000005001f",
          "key": "surface",
          "kind": "guide_surface"
        }
      ],
      "revision": 0,
      "type": "create_guide_surface"
    },
    {
      "definition": {
        "placement": 0,
        "surface": "00000000-0000-4000-8000-00000005001f",
        "thickness": {
          "expression": "",
          "quantity": "length",
          "value": 5
        },
        "correctOpposite": false
      },
      "derivedGroupId": null,
      "displayName": "部品A",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050020",
      "inputEntityIds": [
        "00000000-0000-4000-8000-00000005001f"
      ],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050021",
          "key": "part",
          "kind": "part"
        }
      ],
      "revision": 0,
      "type": "thicken_surface"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": true,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": -40,
                "y": 0,
                "z": 0
              },
              "end": {
                "x": -20,
                "y": 0,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050022"
            },
            {
              "type": "line",
              "start": {
                "x": -20,
                "y": 0,
                "z": 0
              },
              "end": {
                "x": -20,
                "y": 15,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050023"
            },
            {
              "type": "line",
              "start": {
                "x": -20,
                "y": 15,
                "z": 0
              },
              "end": {
                "x": -40,
                "y": 15,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050024"
            },
            {
              "type": "line",
              "start": {
                "x": -40,
                "y": 15,
                "z": 0
              },
              "end": {
                "x": -40,
                "y": 0,
                "z": 0
              },
              "id": "00000000-0000-4000-8000-000000050025"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "輪郭B",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050026",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050027",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "chains": [
          {
            "segments": [
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-000000050022",
                "wireEntityId": "00000000-0000-4000-8000-000000050027"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-000000050023",
                "wireEntityId": "00000000-0000-4000-8000-000000050027"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-000000050024",
                "wireEntityId": "00000000-0000-4000-8000-000000050027"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-000000050025",
                "wireEntityId": "00000000-0000-4000-8000-000000050027"
              }
            ],
            "reversed": [
              false,
              false,
              false,
              false
            ]
          }
        ],
        "roles": [
          0
        ],
        "method": 0,
        "fourEdgeStyle": 0,
        "lockSectionOrder": false,
        "offsetDistanceMm": 0,
        "revolveAngleRad": 0,
        "revolveAxisDirection": {
          "x": 0,
          "y": 0,
          "z": 1
        },
        "revolveAxisPoint": {
          "x": 0,
          "y": 0,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "面B",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050028",
      "inputEntityIds": [
        "00000000-0000-4000-8000-000000050027"
      ],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050029",
          "key": "surface",
          "kind": "guide_surface"
        }
      ],
      "revision": 0,
      "type": "create_guide_surface"
    },
    {
      "definition": {
        "placement": 0,
        "surface": "00000000-0000-4000-8000-000000050029",
        "thickness": {
          "expression": "",
          "quantity": "length",
          "value": 5
        },
        "correctOpposite": false
      },
      "derivedGroupId": null,
      "displayName": "部品B",
      "enabled": true,
      "id": "000)SelectionFixture"+R"SelectionFixture(00000-0000-4000-8000-00000005002a",
      "inputEntityIds": [
        "00000000-0000-4000-8000-000000050029"
      ],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-00000005002b",
          "key": "part",
          "kind": "part"
        }
      ],
      "revision": 0,
      "type": "thicken_surface"
    },
    {
      "definition": {
        "construction": false,
        "sourcePlaneId": null,
        "wire": {
          "closed": true,
          "segments": [
            {
              "type": "line",
              "start": {
                "x": -10,
                "y": 20,
                "z": 8
              },
              "end": {
                "x": 10,
                "y": 20,
                "z": 8
              },
              "id": "00000000-0000-4000-8000-00000005002c"
            },
            {
              "type": "line",
              "start": {
                "x": 10,
                "y": 20,
                "z": 8
              },
              "end": {
                "x": 10,
                "y": 35,
                "z": 8
              },
              "id": "00000000-0000-4000-8000-00000005002d"
            },
            {
              "type": "line",
              "start": {
                "x": 10,
                "y": 35,
                "z": 8
              },
              "end": {
                "x": -10,
                "y": 35,
                "z": 8
              },
              "id": "00000000-0000-4000-8000-00000005002e"
            },
            {
              "type": "line",
              "start": {
                "x": -10,
                "y": 35,
                "z": 8
              },
              "end": {
                "x": -10,
                "y": 20,
                "z": 8
              },
              "id": "00000000-0000-4000-8000-00000005002f"
            }
          ]
        }
      },
      "derivedGroupId": null,
      "displayName": "輪郭C",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050030",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050031",
          "key": "wire",
          "kind": "wire"
        }
      ],
      "revision": 0,
      "type": "create_wire"
    },
    {
      "definition": {
        "chains": [
          {
            "segments": [
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-00000005002c",
                "wireEntityId": "00000000-0000-4000-8000-000000050031"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-00000005002d",
                "wireEntityId": "00000000-0000-4000-8000-000000050031"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-00000005002e",
                "wireEntityId": "00000000-0000-4000-8000-000000050031"
              },
              {
                "range": {
                  "first": 0,
                  "last": 1
                },
                "segmentId": "00000000-0000-4000-8000-00000005002f",
                "wireEntityId": "00000000-0000-4000-8000-000000050031"
              }
            ],
            "reversed": [
              false,
              false,
              false,
              false
            ]
          }
        ],
        "roles": [
          0
        ],
        "method": 0,
        "fourEdgeStyle": 0,
        "lockSectionOrder": false,
        "offsetDistanceMm": 0,
        "revolveAngleRad": 0,
        "revolveAxisDirection": {
          "x": 0,
          "y": 0,
          "z": 1
        },
        "revolveAxisPoint": {
          "x": 0,
          "y": 0,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "面C",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000050032",
      "inputEntityIds": [
        "00000000-0000-4000-8000-000000050031"
      ],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000050033",
          "key": "surface",
          "kind": "guide_surface"
        }
      ],
      "revision": 0,
      "type": "create_guide_surface"
    },
    {
      "definition": {
        "inputs": [],
        "isOriginPlane": true,
        "method": 0,
        "normal": {
          "x": 0,
          "y": 0,
          "z": 1
        },
        "offset": {
          "expression": "",
          "quantity": "scalar",
          "value": 0
        },
        "origin": {
          "x": 55,
          "y": -30,
          "z": 0
        },
        "uDirection": {
          "x": 1,
          "y": 0,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "基準平面1",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000060000",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000060001",
          "key": "plane",
          "kind": "work_plane"
        }
      ],
      "revision": 0,
      "type": "create_work_plane"
    },
    {
      "definition": {
        "inputs": [],
        "isOriginPlane": true,
        "method": 0,
        "normal": {
          "x": 0,
          "y": 0,
          "z": 1
        },
        "offset": {
          "expression": "",
          "quantity": "scalar",
          "value": 0
        },
        "origin": {
          "x": 55,
          "y": 35,
          "z": 10
        },
        "uDirection": {
          "x": 1,
          "y": 0,
          "z": 0
        }
      },
      "derivedGroupId": null,
      "displayName": "基準平面2",
      "enabled": true,
      "id": "00000000-0000-4000-8000-000000060002",
      "inputEntityIds": [],
      "outputs": [
        {
          "entityId": "00000000-0000-4000-8000-000000060003",
          "key": "plane",
          "kind": "work_plane"
        }
      ],
      "revision": 0,
      "type": "create_work_plane"
    }
  ],
  "format": "kachakachaCAD",
  "groups": [
    {
      "childOrder": [
        "00000000-0000-4000-8000-000000050004",
        "00000000-0000-4000-8000-000000050006",
        "00000000-0000-4000-8000-000000050008",
        "00000000-0000-4000-8000-00000005000b",
        "00000000-0000-4000-8000-00000005000e",
        "00000000-0000-4000-8000-000000050011",
        "00000000-0000-4000-8000-000000050014",
        "00000000-0000-4000-8000-000000050017",
        "00000000-0000-4000-8000-00000005001d",
        "00000000-0000-4000-8000-00000005001f",
        "00000000-0000-4000-8000-000000050021",
        "00000000-0000-4000-8000-000000050027",
        "00000000-0000-4000-8000-000000050029",
        "00000000-0000-4000-8000-00000005002b",
        "00000000-0000-4)SelectionFixture"+R"SelectionFixture(000-8000-000000050031",
        "00000000-0000-4000-8000-000000050033"
      ],
      "displayName": "選択試験",
      "id": "00000000-0000-4000-8000-000000050001",
      "parentGroupId": null,
      "state": "visible"
    }
  ],
  "metadata": {
    "author": "",
    "description": "General selection route fixture. Input geometry only.",
    "title": "Selection routes"
  },
  "referenceDimensions": [],
  "revision": 1,
  "rootOrder": [
    "00000000-0000-4000-8000-000000050001"
  ],
  "schemaVersion": 2,
  "tolerances": {
    "candidateMenuPx": 14,
    "displayPickPx": 8,
    "edgePickPx": 6,
    "interactiveJoinMm": 0.01,
    "modelAngularRad": 1e-09,
    "modelLinearMm": 1e-06,
    "numericEpsilon": 1e-12,
    "snapPickPx": 12
  },
  "uiState": {},
  "units": {
    "displayAngle": "deg",
    "length": "mm",
    "storedAngle": "rad"
  }
})SelectionFixture";
static const std::map<std::string,std::string> aliases{
    {"point0","00000000-0000-4000-8000-000000050004"},
    {"point1","00000000-0000-4000-8000-000000050006"},
    {"point2","00000000-0000-4000-8000-000000050008"},
    {"lineA","00000000-0000-4000-8000-00000005000b"},
    {"lineB","00000000-0000-4000-8000-00000005000e"},
    {"axis","00000000-0000-4000-8000-000000050011"},
    {"path","00000000-0000-4000-8000-000000050014"},
    {"circle","00000000-0000-4000-8000-000000050017"},
    {"profile0","00000000-0000-4000-8000-00000005001d"},
    {"guide0","00000000-0000-4000-8000-00000005001f"},
    {"part0","00000000-0000-4000-8000-000000050021"},
    {"profile1","00000000-0000-4000-8000-000000050027"},
    {"guide1","00000000-0000-4000-8000-000000050029"},
    {"part1","00000000-0000-4000-8000-00000005002b"},
    {"profile2","00000000-0000-4000-8000-000000050031"},
    {"guide2","00000000-0000-4000-8000-000000050033"},
    {"plane0","00000000-0000-4000-8000-000000060001"},
    {"plane1","00000000-0000-4000-8000-000000060003"},
};
}
