import dataclasses
import random
import reprlib
import typing

N_FLOORS = 5
SEGMENTS_PER_FLOOR = 10

assert SEGMENTS_PER_FLOOR % 2 == 0


@dataclasses.dataclass
class Floor:
    corridor: int
    vertical_walls: list[bool]
    horizontal_walls: list[bool]


@dataclasses.dataclass
class Tower:
    floors: list[Floor]


tower = Tower(
    floors=[
        Floor(
            corridor=-1,
            vertical_walls=[True] * SEGMENTS_PER_FLOOR,
            horizontal_walls=[True] * SEGMENTS_PER_FLOOR,
        )
        for _i in range(N_FLOORS)
    ]
)

tower.floors[1].corridor = 0
tower.floors[2].corridor = 1

@dataclasses.dataclass(eq=False)
class Node:
    floor: int
    segment: int
    neighbors: list["NeighborNode"]

    @reprlib.recursive_repr()
    def __repr__(self) -> str:
        return super().__repr__()


@dataclasses.dataclass
class NeighborNode:
    node: Node
    is_connected: typing.Callable[[Node], bool]
    set_connected: typing.Callable[[Node], None]

    @reprlib.recursive_repr()
    def __repr__(self) -> str:
        return super().__repr__()


nodes = {
    (_i_floor, _i_segment): Node(_i_floor, _i_segment, [])
    for _i_floor in range(N_FLOORS)
    for _i_segment in range(SEGMENTS_PER_FLOOR)
}

for i_floor in range(N_FLOORS):
    floor = tower.floors[i_floor]
    for i_segment in range(SEGMENTS_PER_FLOOR):
        node = nodes[(i_floor, i_segment)]
        if floor.corridor != -1 and (
            floor.corridor == i_segment
            or (
                (floor.corridor + SEGMENTS_PER_FLOOR // 2) % SEGMENTS_PER_FLOOR
                == i_segment
            )
        ):
            node.neighbors.append(
                NeighborNode(
                    node=nodes[
                        (
                            i_floor,
                            (i_segment + SEGMENTS_PER_FLOOR // 2) % SEGMENTS_PER_FLOOR,
                        )
                    ],
                    is_connected=lambda fn: True,
                    set_connected=lambda fn: None,
                )
            )
        if i_floor > 0:

            def is_connected(fn: Node):
                return not tower.floors[fn.floor - 1].horizontal_walls[fn.segment]

            def set_connected(fn: Node):
                tower.floors[fn.floor - 1].horizontal_walls[fn.segment] = False

            node.neighbors.append(
                NeighborNode(
                    node=nodes[(i_floor - 1, i_segment)],
                    is_connected=is_connected,
                    set_connected=set_connected,
                )
            )
        if i_floor + 1 < N_FLOORS:

            def is_connected(fn: Node):
                return not tower.floors[fn.floor].horizontal_walls[fn.segment]

            def set_connected(fn: Node):
                tower.floors[fn.floor].horizontal_walls[fn.segment] = False

            node.neighbors.append(
                NeighborNode(
                    node=nodes[(i_floor + 1, i_segment)],
                    is_connected=is_connected,
                    set_connected=set_connected,
                )
            )

        def is_connected(fn: Node):
            return not tower.floors[fn.floor].vertical_walls[
                (fn.segment + 1) % SEGMENTS_PER_FLOOR
            ]

        def set_connected(fn: Node):
            tower.floors[fn.floor].vertical_walls[
                (fn.segment + 1) % SEGMENTS_PER_FLOOR
            ] = False

        node.neighbors.append(
            NeighborNode(
                node=nodes[(i_floor, (i_segment + 1) % SEGMENTS_PER_FLOOR)],
                is_connected=is_connected,
                set_connected=set_connected,
            )
        )

        def is_connected(fn: Node):
            return not tower.floors[fn.floor].vertical_walls[fn.segment]

        def set_connected(fn: Node):
            tower.floors[fn.floor].vertical_walls[fn.segment] = False

        node.neighbors.append(
            NeighborNode(
                node=nodes[
                    (i_floor, (i_segment - 1 + SEGMENTS_PER_FLOOR) % SEGMENTS_PER_FLOOR)
                ],
                is_connected=is_connected,
                set_connected=set_connected,
            )
        )


rng = random.Random(421)

iter = 0

while True:
    class_by_node = {node: _class for _class, node in enumerate(nodes.values())}
    for node in nodes.values():
        merge_classes = {
            class_by_node[_neighbor.node]
            for _neighbor in node.neighbors
            if _neighbor.is_connected(node)
        }
        merge_classes.add(class_by_node[node])
        new_class = min(merge_classes)
        for node in nodes.values():
            if class_by_node[node] in merge_classes:
                class_by_node[node] = new_class
    if len(set(class_by_node.values())) == 1:
        print("Done after", iter, "iterations")
        break
    disjointed_neighbors: list[tuple[Node, NeighborNode]] = []
    for node in nodes.values():
        for neighbor in node.neighbors:
            if class_by_node[node] != class_by_node[neighbor.node]:
                disjointed_neighbors.append((node, neighbor))
    fn, nn = rng.choice(disjointed_neighbors)
    nn.set_connected(fn)
    iter += 1


for i_floor in range(N_FLOORS):
    floor = tower.floors[i_floor]
    if floor.corridor != -1:
        print(f"tower_floors[{i_floor}].corridor = {floor.corridor};")
    for i_segment in range(SEGMENTS_PER_FLOOR):
        if floor.vertical_walls[i_segment]:
            print(f"tower_floors[{i_floor}].vertical_walls[{i_segment}] = true;")
        if floor.horizontal_walls[i_segment]:
            print(f"tower_floors[{i_floor}].horizontal_walls[{i_segment}] = true;")
