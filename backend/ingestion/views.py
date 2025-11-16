from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from .serializers import TelemetrySerializer
from .tasks import telemetry_ingest
import logging
from datetime import datetime
logger = logging.getLogger(__name__)


@api_view(['POST'])
@permission_classes([IsAuthenticated])
def telemetry_endpoint(request):
    print("="*80)
    print()
    print()
    print(request.data)
    print()
    print()
    print()
    print("="*80)
    
    
    
    timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    
    print("/n"+"="*80)
    print()
    print()
    
    print(f"[{timestamp}] djangoView incoming http request")
    print("="*80)
    print()
    print()
    print()
    
    
    print(f"\n HTTP Details:")
    print()
    
    print(f"request method: {request.method}")
    print(f"request path: {request.path}")
    
    
    print("/n"+"="*80)
    print(f" Is user authencatied or not")
    print("="*80)
    
    if request.user.is_authenticated:
        
        print(f"User:  {request.user}")
        print(f"Is authenticated")
        print(" token valid")
    else:
        print("not authencated")
    
    
    if request.data:
        print(f"\n Data Structure:")
        print(f"   Keys: {list(request.data.keys())}")
        
        # Show key fields
        print(f"\n Key Fields:")
        if 'agent_id' in request.data:
            print(f"   • Agent ID: {request.data['agent_id']}")
        if 'event_id' in request.data:
            print(f"   • Event ID: {request.data['event_id']}")
        if 'event_type' in request.data:
            print(f"   • Event Type: {request.data['event_type']}")
        if 'timestamp' in request.data:
            print(f"   • Timestamp: {request.data['timestamp']}")
    
        
            
        
        
        
    
    
        
    
    
    serializer=TelemetrySerializer(data=request.data)
    
    if serializer.is_valid():
        print()
        print()
        print()
    
        print(f"validation passed")
        validated_data=serializer.validated_data
        
        print(f"\n Validated Data:")
        print(f"   • Event Type: {validated_data.get('event_type', 'N/A')}")
        print(f"   • Agent: {validated_data.get('agent_id', 'N/A')}")
        print(f"   • Severity: {validated_data.get('severity', 'N/A')}")
        
        
        event_type=validated_data.get("event_type")
        
        
        if event_type == "process":
            print("\n Process Event details:  ")
            if "process" in validated_data:
                proc=validated_data["process"]
                print(f"    Name: {proc.get("name","N/A")}")
                print(f"    pid: {proc.get('pid','N/A')}")
                print(f"    User: {proc.get('user','N/A')}")
                cmd = proc.get('command_line', 'N/A')
                if len(cmd) > 80:
                    print(f"   • Command: {cmd[:80]}...")
                else:
                    print(f"   • Command: {cmd}")
                print(f"   • Action: {proc.get('action', 'N/A')}")
            else:
                print(f"No process data in payload")   
        
        
        elif event_type =="file":
            print("\n file path")
            if 'file' in event_type:
                file_data=validated_data["file"]
                print(f"file path {file_data.get('path',"N/A")}") 
                print(f"file operation {file_data.get('operation',"N/A")}") 
                print(f"file image {file_data.get('image',"N/A")}")
            else:
                print("no file data in payload ")
        elif event_type=="network":
            print("\n network event type")
            if 'network' in event_type:
                net=validated_data.get('network')
                print(f" source: {net.get('source_ip','N/A'):{net.get('source_port','N/A')}}")
                print(f" destination: {net.get('dest_ip','N/A'):{net.get('dest_port','N/A')}}")
                print(f" protocol: {net.get('protocol','N/A')}")
                print(f" Process: {net.get('image', 'N/A')}")
            else:
                print("no network data in the payload")
                
        if 'host' in validated_data:
            host = validated_data['host']
            print(f"\n Host Information:")
            print(f"   • Hostname: {host.get('hostname', 'N/A')}")
            print(f"   • OS: {host.get('os', 'N/A')} {host.get('os_version', 'N/A')}")
        
        
        
        
        print()
        print()
        print()
        print(f"\n Sending to Celery Worker")
        
        try:
            task=telemetry_ingest.delay(validated_data)
            
            print(f" task queued succesfully")
            print(f" task ID: {task.id}")
            print(f" task state: {task.state}")
            
            print(f"\n Event accepted and queued for processing")
            
        
        
        except Exception as e:
            print(f"failed to task the queue{str(e)}")
            logger.error(f"celery worker queuing the task{str(e)}")
        
        
        print("="*30+ "\n")
        
        
        return Response({
            'status': 'accepted',
            'message': 'Telemetry received and queued for processing',
            'event_id': validated_data.get('event_id'),
            'task_id': task.id if 'task' in locals() else None
        }, status=status.HTTP_201_CREATED)                        
    else:
        #
        print(f"  Validation: FAILED")
        print(f"\n Validation Errors:")
        
        # Log each validation error
        for field, errors in serializer.errors.items():
            print(f"    {field}: {errors}")
        
        print("="*80 + "\n")
        
        
        logger.error(f"Validation error: {serializer.errors}")
        logger.error(f"Raw data: {request.data}")
        
        return Response({
            'status': 'error',
            'message': 'Validation failed',
            'errors': serializer.errors
        }, status=status.HTTP_400_BAD_REQUEST)       
        
        
        
    
                
             
                
                
                
                
                
    

