/**
 This file is part of the Apotris multiplayer server implementation.
 Copyright (C) 2024 Isaac Aronson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.
 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import type { Context } from 'https://deno.land/x/oak@v12.6.1/mod.ts';

// Constants
const MAX_PLAYERS = 5; // Max players per room

// Global state for WebSocket connections
const clients: Map<string, WebSocket> = new Map<string, WebSocket>();
const rooms: Map<string, string[]> = new Map<string, string[]>(); // roomID -> array of clientIDs
const roomTypes: Map<string, 'friends' | 'randoms'> = new Map<string, 'friends' | 'randoms'>();
const names: Map<string, string> = new Map<string, string>(); // clientId -> name

export function getOnlinePlayerCounts() {
    let friends = 0;
    let randoms = 0;

    for (const [roomID, members] of rooms) {
        if (roomTypes.get(roomID) === 'randoms') {
            randoms += members.length;
        } else {
            friends += members.length;
        }
    }

    return { friends, randoms, total: friends + randoms };
}

const getRandomInt = (min: number, max: number) => {
    return Math.floor(Math.random() * (max - min + 1)) + min;
};

export async function handleWebSocket(
    ctx: Context & { params: { clientID: string; roomID: string } },
) {
    // Made function async
    const clientID = ctx.params.clientID;
    const requestedRoomID = ctx.params.roomID;

    if (!clientID || !requestedRoomID) {
        ctx.response.status = 400;
        ctx.response.body =
            "Client ID and Room ID/Action (e.g., 'match') are required in the path (e.g., /ws/myclient/myroom).";
        console.log(
            "Client ID and Room ID/Action (e.g., 'match') are required in the path (e.g., /ws/myclient/myroom).",
        );
        return;
    }

    if (!ctx.isUpgradable) {
        ctx.response.status = 400; // Or 426 Upgrade Required
        ctx.response.body = 'Request is not a valid WebSocket upgrade request.';
        console.log(
            `Non-upgradable request from ${clientID} for room ${requestedRoomID}`,
        );
        return;
    }

    try {
        const socket = await ctx.upgrade(); // Oak handles the upgrade and 101 response

        console.info(
            `WS Upgrade successful for client: ${clientID}, requested room/action: ${requestedRoomID}`,
        );

        socket.addEventListener('open', () => {
            handleOpen(clientID, requestedRoomID, socket);
        });

        socket.addEventListener('message', (websocketMessage) => {
            if (typeof websocketMessage.data !== 'string') return;
            handleMessage(websocketMessage, clientID);
        });

        socket.addEventListener('close', (event) => {
            handleClose(event, clientID);
        });

        socket.addEventListener('error', (event) => {
            // deno-lint-ignore no-explicit-any
            const errorMessage = (event as any)?.message || 'Unknown WebSocket error';
            console.error(
                `WebSocket error for client ${clientID}: ${errorMessage}`,
            );
            // The 'close' event will usually follow an error that closes the socket.
        });
    } catch (err) {
        console.error(`Failed to upgrade WebSocket for ${clientID}:`, err);
        // Oak might have already sent a response if ctx.upgrade() itself throws.
        // If not, or if you want to ensure a specific response:
        if (ctx.response.status === 200 || ctx.response.status === undefined) {
            ctx.response.status = 500;
            ctx.response.body = 'WebSocket upgrade failed on server.';
        }
    }
    // No need to manually set ctx.response properties for the 101 handshake,
    // as ctx.upgrade() handles this. For other error paths, ctx.response is set above.
}

function handleMessage(websocketMessage: MessageEvent, clientID: string) {
    console.info(
        `Client ${clientID} :: << :: ${websocketMessage.data.substring(0, 200)}`,
    );

    let message;

    try {
        message = JSON.parse(websocketMessage.data);
    } catch (error) {
        if (error instanceof Error) {
            console.error(
                `Invalid JSON from ${clientID} :: ${error.message}. Data: ${
                    (websocketMessage.data as string).substring(0, 200)
                }`,
            );
        }

        return;
    }

    const destinationID = message.id;

    message.from = clientID;
    const dataToSend = JSON.stringify(message);

    let clientRoomId: string | undefined;
    for (const [roomId, members] of rooms) {
        if (members.includes(clientID)) {
            clientRoomId = roomId;
            break;
        }
    }

    if (destinationID && destinationID !== 'broadcast') {
        const destinationSocket = clients.get(destinationID);
        if (
            !destinationSocket ||
            destinationSocket.readyState !== WebSocket.OPEN
        ) {
            console.warn(
                `Client ${destinationID} not found or not open. Message from ${clientID} not sent.`,
            );
            return;
        }

        console.info(
            `Client ${destinationID} :: >> :: ${dataToSend.substring(0, 200)}`,
        );

        destinationSocket.send(dataToSend);

        return;
    }

    if (destinationID === 'broadcast' || message.type === 'broadcast') {
        if (!clientRoomId) {
            console.warn(
                `Client ${clientID} tried to broadcast but is not in a room.`,
            );
            return;
        }

        const roomMembers = rooms.get(clientRoomId);

        if (!roomMembers) {
            console.warn(`No room members in room ${clientRoomId}`);
            return;
        }

        console.info(
            `Broadcasting message from ${clientID} to room ${clientRoomId}`,
        );

        if (message.type === 'playerInfo') {
            names.set(clientID, message.name);
        }

        roomMembers.forEach((memberId) => {
            if (memberId === clientID) return;

            const memberSocket = clients.get(memberId);

            if (!memberSocket || memberSocket.readyState !== WebSocket.OPEN) {
                return;
            }

            memberSocket.send(dataToSend);
        });

        const clientSocket = clients.get(clientID);

        if (!clientSocket || clientSocket.readyState !== WebSocket.OPEN) return;

        if (message.type == 'playerInfo') {
            roomMembers.forEach((member) => {
                const infoMessage = JSON.stringify({
                    id: 'broadcast',
                    from: member,
                    name: names.get(member),
                    type: 'playerInfo',
                });

                console.log(
                    `Sending player info ${infoMessage} to clientId ${clientID}`,
                );

                clientSocket.send(infoMessage);
            });
        }

        return;
    }

    console.warn(
        `Message from ${clientID} without a clear destination: ${websocketMessage.data.substring(0, 200)}`,
    );
}

function handleOpen(
    clientID: string,
    requestedRoomID: string,
    socket: WebSocket,
) {
    clients.set(clientID, socket);
    console.info(`Client connected :: ${clientID}`);

    let roomIDToJoin: string | undefined;

    // join/create room
    if (requestedRoomID.toLowerCase() === 'match') {
        // matchmake
        for (const [id, room] of rooms.entries()) {
            if (roomTypes.get(id) === 'randoms' && room.length < 2) {
                room.push(clientID);
                roomIDToJoin = id;
                break;
            }
        }
        if (!roomIDToJoin) {
            roomIDToJoin = `room-match-${Math.floor(Math.random() * 100000)}`;
            rooms.set(roomIDToJoin, [clientID]);
            roomTypes.set(roomIDToJoin, 'randoms');
            console.info(
                `New match room created: ${roomIDToJoin} for client ${clientID}`,
            );
        }
    } else {
        // custom room
        const targetRoomID = requestedRoomID.toLowerCase();
        const room = rooms.get(targetRoomID);
        if (room) {
            if (room.length >= MAX_PLAYERS) {
                socket.send(
                    JSON.stringify({
                        type: 'full',
                        id: targetRoomID,
                        message: `Room ${targetRoomID} is full.`,
                    }),
                );
                socket.close(1008, 'Room is full');
                // No need to delete from clients here, 'close' event will handle
                return;
            }
            room.push(clientID);
            roomIDToJoin = targetRoomID;
        } else {
            roomIDToJoin = targetRoomID;
            if (roomIDToJoin) {
                rooms.set(roomIDToJoin, [clientID]);
                roomTypes.set(roomIDToJoin, 'friends');
            }
            console.info(
                `New room created: ${roomIDToJoin} for client ${clientID}`,
            );
        }
    }

    if (!roomIDToJoin) {
        console.error(`Client ${clientID} could not be placed in a room.`);
        socket.close(1011, 'Internal server error - could not assign room.');
        // No need to delete from clients here, 'close' event will handle
        return;
    }

    console.info(`Client ${clientID} joined room ${roomIDToJoin}`);

    const currentRoom = rooms.get(roomIDToJoin);
    if (currentRoom && currentRoom.length > 1) {
        const seed = getRandomInt(1, 2000000000);
        console.info(
            `Room ${roomIDToJoin} has ${currentRoom.length} players. Initiating match setup with seed ${seed}.`,
        );
        currentRoom.forEach((memberClientID, index) => {
            const memberSocket = clients.get(memberClientID);
            if (!memberSocket || memberSocket.readyState !== WebSocket.OPEN) {
                return;
            }

            memberSocket.send(
                JSON.stringify({
                    type: 'match',
                    id: memberClientID,
                    iter: index,
                    room: roomIDToJoin,
                }),
            );
            currentRoom.forEach((otherMemberClientID, otherIndex) => {
                if (memberClientID !== otherMemberClientID) {
                    memberSocket.send(
                        JSON.stringify({
                            type: 'match',
                            id: otherMemberClientID,
                            iter: otherIndex,
                            room: roomIDToJoin,
                        }),
                    );
                }
            });
            memberSocket.send(
                JSON.stringify({
                    type: 'seed',
                    id: seed.toString(),
                    room: roomIDToJoin,
                }),
            );
            console.info(
                `Sent match/seed info to ${memberClientID} in room ${roomIDToJoin}`,
            );
        });
    } else if (currentRoom) {
        socket.send(
            JSON.stringify({
                type: 'match',
                id: clientID,
                iter: 0,
                room: roomIDToJoin,
            }),
        );
        console.log(
            `Room ${roomIDToJoin} has ${currentRoom.length} player(s). Waiting for more.`,
        );
    }
}

function handleClose(event: CloseEvent, clientID: string) {
    clients.delete(clientID);
    console.info(
        `Client disconnected :: ${clientID} (Code: ${event.code}, Reason: ${event.reason || 'N/A'})`,
    );

    for (const [roomID, roomMembers] of rooms.entries()) {
        const index = roomMembers.indexOf(clientID);
        if (index === -1) {
            continue;
        }

        roomMembers.splice(index, 1);
        console.info(`Client ${clientID} removed from room ${roomID}.`);

        if (roomMembers.length === 0) {
            rooms.delete(roomID);
            roomTypes.delete(roomID);
            console.info(`Room ${roomID} is empty and has been removed.`);
            break;
        }

        roomMembers.forEach((memberClientID) => {
            const memberSocket = clients.get(memberClientID);
            if (!memberSocket || memberSocket.readyState !== WebSocket.OPEN) {
                return;
            }

            memberSocket.send(
                JSON.stringify({
                    type: 'close',
                    id: clientID,
                    room: roomID,
                    from: 'server',
                }),
            );
            console.info(
                `Notified ${memberClientID} in room ${roomID} that ${clientID} quit.`,
            );
        });
        break;
    }
}
