"""
Redis Monitoring Script for Stress Tests
Monitor Redis activity during stress testing in real-time
"""

import subprocess
import time
import sys

def monitor_redis_keys():
    """Monitor Redis rate limiting keys"""
    print("="*70)
    print("REDIS RATE LIMITING MONITOR")
    print("="*70)
    print("\nPress Ctrl+C to stop monitoring\n")
    
    try:
        while True:
            # Get all rate limiting keys
            result = subprocess.run(
                ['redis-cli', '-n', '1', 'KEYS', 'rl:*'],
                capture_output=True,
                text=True
            )
            
            keys = [k for k in result.stdout.strip().split('\n') if k]
            
            # Clear screen (Windows)
            subprocess.run('cls', shell=True)
            
            print(f"{'='*70}")
            print(f"REDIS MONITORING - {time.strftime('%H:%M:%S')}")
            print(f"{'='*70}\n")
            print(f"Total Rate Limit Keys: {len(keys)}\n")
            
            if keys:
                print("Active Rate Limits:")
                print("-" * 70)
                
                for key in keys[:20]:  # Show first 20
                    # Get value and TTL
                    value_result = subprocess.run(
                        ['redis-cli', '-n', '1', 'GET', key],
                        capture_output=True,
                        text=True
                    )
                    ttl_result = subprocess.run(
                        ['redis-cli', '-n', '1', 'TTL', key],
                        capture_output=True,
                        text=True
                    )
                    
                    value = value_result.stdout.strip()
                    ttl = ttl_result.stdout.strip()
                    
                    # Parse key
                    key_parts = key.split(':')
                    if len(key_parts) >= 4:
                        func_name = key_parts[2] if len(key_parts) > 2 else 'unknown'
                        agent_id = key_parts[3] if len(key_parts) > 3 else 'unknown'
                        
                        print(f"Function: {func_name}")
                        print(f"  Agent/User: {agent_id}")
                        print(f"  Count: {value}")
                        print(f"  TTL: {ttl}s")
                        print()
                
                if len(keys) > 20:
                    print(f"... and {len(keys) - 20} more keys")
            else:
                print("No active rate limits")
            
            print(f"\n{'='*70}")
            print("Refreshing in 2 seconds... (Ctrl+C to stop)")
            print(f"{'='*70}")
            
            time.sleep(2)
            
    except KeyboardInterrupt:
        print("\n\nMonitoring stopped.")
        sys.exit(0)
    except FileNotFoundError:
        print("\n❌ Error: redis-cli not found in PATH")
        print("   Make sure Redis is installed and redis-cli is accessible")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Error: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    monitor_redis_keys()
