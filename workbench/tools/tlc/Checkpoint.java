import com.sun.tools.attach.VirtualMachine;
import javax.management.ObjectName;
import javax.management.remote.JMXConnectorFactory;
import javax.management.remote.JMXServiceURL;

// Local attach avoids opening an unauthenticated network management port.
class Checkpoint {
    public static void main(String[] args) throws Exception {
        var vm = VirtualMachine.attach(args[0]);
        String address;
        try {
            address = vm.startLocalManagementAgent();
        } finally {
            vm.detach();
        }
        try (var connection = JMXConnectorFactory.connect(new JMXServiceURL(address))) {
            connection.getMBeanServerConnection().invoke(
                new ObjectName("tlc2.tool:type=ModelChecker"), "checkpoint", null, null);
        }
    }
}
