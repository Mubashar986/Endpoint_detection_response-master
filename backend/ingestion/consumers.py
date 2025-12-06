# ingestion/consumers.py
"""
WebSocket Consumer for EDR Agents
=================================
This handles real-time WebSocket connections from EDR agents.

How it works:
1. Agent connects to ws://server:8000/ws/agent/
2. Consumer accepts and adds agent to "agents" group
3. When dashboard sends command, Consumer pushes it to agent
4. Agent responds, Consumer processes the response
"""

import json
import logging
from channels.generic.websocket import AsyncWebsocketConsumer
from channels.db import database_sync_to_async
from asgiref.sync import sync_to_async

logger = logging.getLogger(__name__)


class AgentConsumer(AsyncWebsocketConsumer):
    """
    Handles WebSocket connections from EDR agents.
    
    Connection URL: ws://server:8000/ws/agent/
    
    Message Format (Server → Agent):
    {
        "type": "command",
        "command_id": "uuid",
        "action": "kill_process",
        "parameters": {"pid": 1234}
    }
    
    Message Format (Agent → Server):
    {
        "type": "response",
        "command_id": "uuid",
        "status": "success|failed",
        "result": {...}
    }
    """
    
    async def connect(self):
        """
        Called when an agent establishes a WebSocket connection.
        
        Actions:
        1. Accept the connection
        2. Add agent to "agents" group for broadcast commands
        3. Log the connection
        """
        # Accept the WebSocket connection
        await self.accept()
        
        # Add this consumer to the "agents" group
        # All connected agents will be in this group
        await self.channel_layer.group_add(
            "agents",           # Group name
            self.channel_name   # This consumer's unique channel ID
        )
        
        logger.info(f"[WebSocket] Agent connected: {self.channel_name}")
        print(f"[+] Agent connected: {self.channel_name}")
        
        # Send a welcome message to confirm connection
        await self.send(text_data=json.dumps({
            "type": "connection_established",
            "message": "Connected to EDR Server",
            "channel": self.channel_name
        }))
    
    async def disconnect(self, close_code):
        """
        Called when an agent disconnects.
        
        Actions:
        1. Remove agent from "agents" group
        2. Log the disconnection
        
        Args:
            close_code: WebSocket close code (1000 = normal, 1006 = abnormal)
        """
        # Remove from the "agents" group
        await self.channel_layer.group_discard(
            "agents",
            self.channel_name
        )
        
        logger.info(f"[WebSocket] Agent disconnected: {self.channel_name} (code: {close_code})")
        print(f"[-] Agent disconnected: {self.channel_name} (code: {close_code})")
    
    async def receive(self, text_data):
        """
        Called when agent sends a message to the server.
        
        This handles:
        - Command responses from agent
        - Heartbeat messages
        - Any agent-initiated communication
        
        Args:
            text_data: JSON string from agent
        """
        try:
            data = json.loads(text_data)
            message_type = data.get("type", "unknown")
            
            logger.info(f"[WebSocket] Received from agent: {message_type}")
            print(f"[←] Received from agent: {data}")
            
            if message_type == "response":
                # Agent is responding to a command
                await self.handle_command_response(data)
            
            elif message_type == "heartbeat":
                # Agent heartbeat - respond with ack
                await self.send(text_data=json.dumps({
                    "type": "heartbeat_ack",
                    "timestamp": data.get("timestamp")
                }))
            
            else:
                # Unknown message type - log it
                logger.warning(f"[WebSocket] Unknown message type: {message_type}")
                
        except json.JSONDecodeError as e:
            logger.error(f"[WebSocket] Invalid JSON received: {e}")
            await self.send(text_data=json.dumps({
                "type": "error",
                "message": "Invalid JSON format"
            }))
    
    async def handle_command_response(self, data):
        """
        Process a command response from the agent.
        
        Args:
            data: {
                "type": "response",
                "command_id": "uuid",
                "status": "success|failed",
                "message": "Result message"
            }
        """
        command_id = data.get("command_id")
        status = data.get("status")
        message = data.get("message", "")
        
        logger.info(f"[WebSocket] Command {command_id} completed: {status}")
        print(f"[✓] Command {command_id} completed: {status}")
        print(f"    Message: {message}")
        
        # Update command status in database
        if command_id:
            await self.update_command_status(command_id, status, message)
    
    @database_sync_to_async
    def update_command_status(self, command_id, status, message):
        """
        Update the PendingCommand and ResponseAction in MongoDB.
        
        This is called when the agent responds via WebSocket.
        """
        from django.utils import timezone
        from .models_mongo import PendingCommand, ResponseAction
        
        try:
            # Update PendingCommand
            command = PendingCommand.objects(command_id=command_id).first()
            if command:
                command.status = 'completed' if status == 'success' else 'failed'
                command.result = {'status': status, 'message': message}
                command.completed_at = timezone.now()
                command.save()
                logger.info(f"[WebSocket] Updated PendingCommand {command_id} to {command.status}")
            else:
                logger.warning(f"[WebSocket] PendingCommand {command_id} not found")
            
            # Update ResponseAction (Audit Log)
            action = ResponseAction.objects(command_id=command_id).first()
            if action:
                action.status = 'completed' if status == 'success' else 'failed'
                action.result_summary = message
                action.save()
                logger.info(f"[WebSocket] Updated ResponseAction {command_id}")
            else:
                logger.warning(f"[WebSocket] ResponseAction {command_id} not found")
                
        except Exception as e:
            logger.error(f"[WebSocket] Database update error: {e}")
    
    # =========================================================
    # Handler for commands sent FROM the server TO the agent
    # =========================================================
    
    async def agent_command(self, event):
        """
        Handler for 'agent.command' type messages.
        
        This is called when the server wants to send a command to the agent.
        Triggered by: channel_layer.group_send("agents", {"type": "agent.command", ...})
        
        The "type" value "agent.command" is converted to method name "agent_command"
        (dots become underscores).
        
        Args:
            event: {
                "type": "agent.command",
                "command": {
                    "command_id": "uuid",
                    "action": "kill_process",
                    "parameters": {"pid": 1234}
                }
            }
        """
        command = event.get("command", {})
        
        logger.info(f"[WebSocket] Sending command to agent: {command}")
        print(f"[→] Sending command to agent: {command}")
        
        # Send the command to the connected agent
        await self.send(text_data=json.dumps({
            "type": "command",
            **command
        }))
