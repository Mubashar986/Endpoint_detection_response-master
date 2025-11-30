"""
Comprehensive Rate Limiting Stress Test Suite
Tests rate limiting under various load conditions and identifies failures
"""

import concurrent.futures
import time
import json
import threading
from datetime import datetime
from collections import defaultdict
# import urllib.request
# import urllib.error
# import http.client

# Configuration
BASE_URL = "http://localhost:8000"
AUTH_TOKEN = "55e3993a3633207b78a7e479c93213a3dc21223a"

# Test results storage
results = {
    'total_requests': 0,
    'successful': 0,
    'rate_limited': 0,
    'errors': 0,
    'response_times': [],
    'error_details': defaultdict(list),
    'rate_limit_details': []
}
results_lock = threading.Lock()

class StressTestReporter:
    """Handles test reporting and metrics"""
    
    def __init__(self):
        self.start_time = None
        self.test_name = ""
    
    def start_test(self, name):
        """Start a test scenario"""
        self.test_name = name
        self.start_time = time.time()
        print(f"\n{'='*70}")
        print(f"TEST: {name}")
        print(f"Started: {datetime.now().strftime('%H:%M:%S')}")
        print(f"{'='*70}\n")
    
    def end_test(self):
        """End test and print summary"""
        duration = time.time() - self.start_time
        print(f"\n{'='*70}")
        print(f"TEST COMPLETE: {self.test_name}")
        print(f"Duration: {duration:.2f}s")
        print(f"Total Requests: {results['total_requests']}")
        print(f"✅ Successful: {results['successful']}")
        print(f"🚫 Rate Limited (429): {results['rate_limited']}")
        print(f"❌ Errors: {results['errors']}")
        
        if results['response_times']:
            avg_time = sum(results['response_times']) / len(results['response_times'])
            max_time = max(results['response_times'])
            min_time = min(results['response_times'])
            print(f"\nResponse Times:")
            print(f"  Average: {avg_time*1000:.2f}ms")
            print(f"  Min: {min_time*1000:.2f}ms")
            print(f"  Max: {max_time*1000:.2f}ms")
        
        if results['error_details']:
            print(f"\nError Breakdown:")
            for error_type, instances in results['error_details'].items():
                print(f"  {error_type}: {len(instances)}")
        
        print(f"{'='*70}\n")
        
        # Reset for next test
        self.reset_results()
    
    def reset_results(self):
        """Reset results for next test"""
        results['total_requests'] = 0
        results['successful'] = 0
        results['rate_limited'] = 0
        results['errors'] = 0
        results['response_times'] = []
        results['error_details'] = defaultdict(list)
        results['rate_limit_details'] = []

reporter = StressTestReporter()

import requests

# Global session for connection pooling
session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=100, pool_maxsize=100)
session.mount('http://', adapter)

def make_request(url, headers, data=None, method='POST'):
    """Make HTTP request and track metrics using requests.Session"""
    start_time = time.time()
    
    try:
        if method == 'POST':
            response = session.post(url, json=data, headers=headers, timeout=10)
        else:
            response = session.get(url, headers=headers, timeout=10)
            
        response_time = time.time() - start_time
        status_code = response.status_code
        
        with results_lock:
            results['total_requests'] += 1
            if status_code in [200, 201]:
                results['successful'] += 1
            elif status_code == 429:
                results['rate_limited'] += 1
                try:
                    results['rate_limit_details'].append({
                        'time': datetime.now().isoformat(),
                        'response': response.text
                    })
                except:
                    pass
            else:
                results['errors'] += 1
                results['error_details'][f'HTTP {status_code}'].append(response.text)
                
            results['response_times'].append(response_time)
        
        return {'status': status_code, 'body': response.text, 'time': response_time}
            
    except Exception as e:
        response_time = time.time() - start_time
        
        with results_lock:
            results['total_requests'] += 1
            results['errors'] += 1
            results['error_details'][type(e).__name__].append(str(e))
        
        return {'status': 0, 'error': str(e), 'time': response_time}

def test1_single_agent_sustained_load():
    """
    TEST 1: Single Agent - Sustained Load
    Send 1500 requests over 60 seconds (25 req/sec)
    Expected: First 1000 succeed, then rate limited
    """
    reporter.start_test("Single Agent - Sustained Load (1500 requests in 60s)")
    
    url = f"{BASE_URL}/api/v1/telemetry/"
    headers = {
        'Authorization': f'Token {AUTH_TOKEN}',
        'X-Agent-Token': 'STRESS-TEST-SUSTAINED',
        'Content-Type': 'application/json'
    }
    
    total_requests = 1500
    duration = 60  # seconds
    delay = duration / total_requests
    
    for i in range(total_requests):
        data = {
            'agent_id': 'STRESS-TEST-SUSTAINED',
            'event_id': f'sustained-{i}',
            'event_type': 'file',
            'timestamp': int(time.time()),
            'severity': 'INFO',
            'version': '1.0',
            'host': {'hostname': 'stress-test', 'os': 'Windows'},
            'file': {'path': f'C:\\test-{i}.txt', 'operation': 'write'}
        }
        
        result = make_request(url, headers, data)
        
        # Print progress every 100 requests
        if (i + 1) % 100 == 0:
            print(f"Progress: {i+1}/{total_requests} - "
                  f"Success: {results['successful']}, "
                  f"Rate Limited: {results['rate_limited']}, "
                  f"Errors: {results['errors']}")
        
        time.sleep(delay)
    
    reporter.end_test()

def test2_single_agent_burst():
    """
    TEST 2: Single Agent - Burst Traffic
    Send 300 requests as fast as possible
    Expected: First ~200 succeed (burst limit), then rate limited
    """
    reporter.start_test("Single Agent - Burst Traffic (300 rapid requests)")
    
    url = f"{BASE_URL}/api/v1/telemetry/"
    headers = {
        'Authorization': f'Token {AUTH_TOKEN}',
        'X-Agent-Token': 'STRESS-TEST-BURST',
        'Content-Type': 'application/json'
    }
    
    for i in range(300):
        data = {
            'agent_id': 'STRESS-TEST-BURST',
            'event_id': f'burst-{i}',
            'event_type': 'file',
            'timestamp': int(time.time()),
            'severity': 'INFO',
            'version': '1.0',
            'host': {'hostname': 'stress-test', 'os': 'Windows'},
            'file': {'path': f'C:\\burst-{i}.txt', 'operation': 'write'}
        }
        
        result = make_request(url, headers, data)
        
        if (i + 1) % 50 == 0:
            print(f"Progress: {i+1}/300 - "
                  f"Success: {results['successful']}, "
                  f"Rate Limited: {results['rate_limited']}")
        
        # Small delay to avoid overwhelming the server completely
        time.sleep(0.01)
    
    reporter.end_test()

def agent_worker(agent_id, num_requests):
    """Worker function for concurrent agent test"""
    url = f"{BASE_URL}/api/v1/telemetry/"
    headers = {
        'Authorization': f'Token {AUTH_TOKEN}',
        'X-Agent-Token': agent_id,
        'Content-Type': 'application/json'
    }
    
    for i in range(num_requests):
        data = {
            'agent_id': agent_id,
            'event_id': f'{agent_id}-{i}',
            'event_type': 'file',
            'timestamp': int(time.time()),
            'severity': 'INFO',
            'version': '1.0',
            'host': {'hostname': f'host-{agent_id}', 'os': 'Windows'},
            'file': {'path': f'C:\\test-{i}.txt', 'operation': 'write'}
        }
        
        make_request(url, headers, data)
        time.sleep(0.05)  # 20 req/sec per agent

def test3_multiple_agents_concurrent():
    """
    TEST 3: Multiple Agents - Concurrent Access
    10 agents each send 100 requests simultaneously
    Expected: Each agent gets their own quota (isolation test)
    """
    reporter.start_test("Multiple Agents - Concurrent Access (10 agents × 100 requests)")
    
    num_agents = 10
    requests_per_agent = 100
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=num_agents) as executor:
        futures = []
        for i in range(num_agents):
            agent_id = f'CONCURRENT-AGENT-{i:02d}'
            future = executor.submit(agent_worker, agent_id, requests_per_agent)
            futures.append(future)
        
        # Wait for all agents to complete
        completed = 0
        for future in concurrent.futures.as_completed(futures):
            completed += 1
            print(f"Agent completed: {completed}/{num_agents} - "
                  f"Total Success: {results['successful']}, "
                  f"Rate Limited: {results['rate_limited']}")
    
    reporter.end_test()

def test4_rate_limit_boundary():
    """
    TEST 4: Rate Limit Boundary Test
    Send exactly 1000 requests in one minute, then 1 more
    Expected: First 1000 succeed, 1001st is rate limited
    """
    reporter.start_test("Rate Limit Boundary Test (exactly 1001 requests in 60s)")
    
    url = f"{BASE_URL}/api/v1/telemetry/"
    headers = {
        'Authorization': f'Token {AUTH_TOKEN}',
        'X-Agent-Token': 'BOUNDARY-TEST',
        'Content-Type': 'application/json'
    }
    
    num_requests = 1001
    duration = 60
    delay = duration / num_requests
    
    for i in range(num_requests):
        data = {
            'agent_id': 'BOUNDARY-TEST',
            'event_id': f'boundary-{i}',
            'event_type': 'file',
            'timestamp': int(time.time()),
            'severity': 'INFO',
            'version': '1.0',
            'host': {'hostname': 'boundary-test', 'os': 'Windows'},
            'file': {'path': f'C:\\test.txt', 'operation': 'write'}
        }
        
        result = make_request(url, headers, data)
        
        # Check if this is the boundary request
        if i == 1000:
            if result.get('status') == 429:
                print(f"\n✅ BOUNDARY CORRECTLY ENFORCED at request {i+1}")
            else:
                print(f"\n⚠️  WARNING: Request {i+1} was NOT rate limited (expected 429)")
        
        time.sleep(delay)
    
    reporter.end_test()

def test5_recovery_after_limit():
    """
    TEST 5: Recovery After Rate Limit
    Hit rate limit, wait for window to reset, verify access restored
    """
    reporter.start_test("Recovery After Rate Limit (rate limit → wait → retry)")
    
    url = f"{BASE_URL}/api/v1/telemetry/"
    headers = {
        'Authorization': f'Token {AUTH_TOKEN}',
        'X-Agent-Token': 'RECOVERY-TEST',
        'Content-Type': 'application/json'
    }
    
    print("Phase 1: Hitting rate limit (sending 250 rapid requests)...")
    for i in range(250):
        data = {
            'agent_id': 'RECOVERY-TEST',
            'event_id': f'recovery-phase1-{i}',
            'event_type': 'file',
            'timestamp': int(time.time()),
            'severity': 'INFO',
            'version': '1.0',
            'host': {'hostname': 'recovery-test', 'os': 'Windows'},
            'file': {'path': 'C:\\test.txt', 'operation': 'write'}
        }
        make_request(url, headers, data)
        time.sleep(0.01)
    
    rate_limited = results['rate_limited']
    print(f"✅ Phase 1 complete - Rate limited: {rate_limited} times")
    
    if rate_limited > 0:
        print("\nPhase 2: Waiting 70 seconds for rate limit window to reset...")
        for remaining in range(70, 0, -10):
            print(f"  Waiting... {remaining}s remaining")
            time.sleep(10)
        
        print("\nPhase 3: Testing if access is restored...")
        phase2_start_limited = results['rate_limited']
        
        for i in range(10):
            data = {
                'agent_id': 'RECOVERY-TEST',
                'event_id': f'recovery-phase3-{i}',
                'event_type': 'file',
                'timestamp': int(time.time()),
                'severity': 'INFO',
                'version': '1.0',
                'host': {'hostname': 'recovery-test', 'os': 'Windows'},
                'file': {'path': 'C:\\test.txt', 'operation': 'write'}
            }
            result = make_request(url, headers, data)
            
            if result.get('status') in [200, 201]:
                print(f"  Request {i+1}: ✅ Success - Access restored!")
            elif result.get('status') == 429:
                print(f"  Request {i+1}: 🚫 Still rate limited")
        
        if results['rate_limited'] == phase2_start_limited:
            print("\n✅ RECOVERY SUCCESSFUL - Rate limit reset after window expired")
        else:
            print("\n⚠️  Still encountering rate limits after window")
    else:
        print("\n⚠️  Did not hit rate limit in Phase 1, test inconclusive")
    
    reporter.end_test()

def test6_mixed_endpoint_stress():
    """
    TEST 6: Mixed Endpoint Stress Test
    Simultaneously stress telemetry, dashboard, and response action endpoints
    """
    reporter.start_test("Mixed Endpoint Stress (realistic multi-endpoint load)")
    
    def telemetry_worker():
        url = f"{BASE_URL}/api/v1/telemetry/"
        headers = {
            'Authorization': f'Token {AUTH_TOKEN}',
            'X-Agent-Token': 'MIXED-TELEMETRY',
            'Content-Type': 'application/json'
        }
        for i in range(50):
            data = {
                'agent_id': 'MIXED-TELEMETRY',
                'event_id': f'mixed-{i}',
                'event_type': 'file',
                'timestamp': int(time.time()),
                'severity': 'INFO',
                'version': '1.0',
                'host': {'hostname': 'mixed-test', 'os': 'Windows'},
                'file': {'path': 'C:\\test.txt', 'operation': 'write'}
            }
            make_request(url, headers, data)
            time.sleep(0.1)
    
    def dashboard_worker():
        url = f"{BASE_URL}/api/v1/dashboard/stats/"
        headers = {'Authorization': f'Token {AUTH_TOKEN}'}
        for i in range(30):
            make_request(url, headers, method='GET')
            time.sleep(0.2)
    
    def response_action_worker():
        url = f"{BASE_URL}/api/v1/response/kill_process/"
        headers = {
            'Authorization': f'Token {AUTH_TOKEN}',
            'Content-Type': 'application/json'
        }
        for i in range(15):
            data = {
                'agent_id': 'MIXED-TEST',
                'pid': 9999 + i,
                'reason': 'Stress test'
            }
            make_request(url, headers, data)
            time.sleep(0.3)
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
        futures = [
            executor.submit(telemetry_worker),
            executor.submit(dashboard_worker),
            executor.submit(response_action_worker)
        ]
        
        for future in concurrent.futures.as_completed(futures):
            future.result()
    
    reporter.end_test()

def main():
    """Run all stress tests"""
    print("\n" + "="*70)
    print("RATE LIMITING STRESS TEST SUITE")
    print("="*70)
    print(f"Started: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"Target: {BASE_URL}")
    print("="*70)
    
    # Check server availability
    try:
        response = requests.get(f"{BASE_URL}/api/v1/health/", timeout=5)
        if response.status_code == 200:
            print("\n✅ Server is running and responding\n")
        else:
            print(f"\n⚠️  Server responded with status {response.status_code}\n")
    except Exception as e:
        print(f"\n❌ Cannot connect to server: {str(e)}")
        print("   Make sure Django server is running")
        return
    
    # Run all tests
    tests = [
        ("1", "Single Agent - Sustained Load", test1_single_agent_sustained_load),
        ("2", "Single Agent - Burst Traffic", test2_single_agent_burst),
        ("3", "Multiple Agents - Concurrent", test3_multiple_agents_concurrent),
        ("4", "Rate Limit Boundary", test4_rate_limit_boundary),
        ("5", "Recovery After Limit", test5_recovery_after_limit),
        ("6", "Mixed Endpoint Stress", test6_mixed_endpoint_stress),
    ]
    
    import sys
    
    # Check for command line arguments
    if len(sys.argv) > 1 and sys.argv[1] == '--all':
        choice = '7'
        print("Running ALL tests (command line argument)")
    else:
        print("Available Tests:")
        for num, name, _ in tests:
            print(f"  {num}. {name}")
        print("  7. Run ALL tests")
        print()
        
        choice = input("Select test to run (1-7): ").strip()
    
    if choice == '7':
        for num, name, test_func in tests:
            test_func()
            print("\n⏸  Pausing 10 seconds before next test...")
            time.sleep(10)
    elif choice in ['1', '2', '3', '4', '5', '6']:
        test_func = tests[int(choice) - 1][2]
        test_func()
    else:
        print("Invalid choice")
        return
    
    print("\n" + "="*70)
    print("ALL TESTS COMPLETE")
    print("="*70)
    print(f"Completed: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("="*70)

if __name__ == "__main__":
    main()
